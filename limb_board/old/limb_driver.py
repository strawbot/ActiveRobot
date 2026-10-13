"""
limb_driver.py — 4-channel SMA (muscle wire) driver
AD5593R I2C DAC/ADC + MCP6002 current-control loop + IRLML6344 FETs

Three safety layers on every move() call:
  1. High-water inhibit  — block if accumulated fault score >= threshold
  2. Presence check      — probe with a tiny setpoint; reject if ADC rails
  3. Response validation — after settling, confirm ADC reflects the command

Faults are weighted; any combination that crosses HIGH_WATER_MARK triggers
an error-level alert and locks the channel until reset_faults() is called.
"""

import time
import logging
from dataclasses import dataclass, field
from typing import Optional

log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Board constants (from schematic / README_2.txt)
# ---------------------------------------------------------------------------

ADDR_DEFAULT    = 0x10        # I2C address, A0 low (0x11 if A0 high)

VREF_V          = 2.5         # Internal reference voltage
ADC_RANGE_V     = 5.0         # ADC_RANGE=1: 0..2*VREF
ADC_BITS        = 12
ADC_MAX         = (1 << ADC_BITS) - 1   # 4095

SENSE_OHMS      = 1.0         # 1R0 sense resistors — I = V_SET / 1.0
MAX_SET_MV      = 300.0       # Hard current cap: 300 mV → 300 mA
WIRE_SAT_MV     = 233.0       # ~saturation knee at 42 Ω cold wire + 10 V supply

NUM_CHANNELS    = 4

# AD5593R register addresses
_REG_GEN_CTRL   = 0x03
_REG_ADC_CONFIG = 0x04
_REG_DAC_CONFIG = 0x05
_REG_PD_REF     = 0x0B
_REG_ADC_READ   = 0x40       # AD5593R ADC result readback address

# ADC rail thresholds — op-amp output near VCC when loop unsatisfied (no wire)
# With 0..5 V ADC range: VCC=3.3 V → ~2703 counts, 5 V → 4095.
# Flag as "railed" above 3700 to catch both supply voltages conservatively.
ADC_RAILED_THRESHOLD = 3700   # counts: op-amp near VCC → limb absent or open
ADC_FLOOR_THRESHOLD  = 40     # counts: stuck at zero

# Presence-probe parameters
PROBE_SET_MV    = 50.0        # Small safe setpoint for presence test (50 mA)
PROBE_SETTLE_S  = 0.06        # 60 ms: >> loop bandwidth, << thermal time const

# Response-validation settle time
DEFAULT_SETTLE_S = 0.15

# ---------------------------------------------------------------------------
# Fault system
# ---------------------------------------------------------------------------

# Weighted fault types.  Sum >= HIGH_WATER_MARK → channel inhibited.
FAULT_WEIGHTS: dict[str, int] = {
    "absent":           10,   # probe showed limb not connected
    "overcurrent":       8,   # ADC implies current well above setpoint
    "no_response":       5,   # ADC didn't move at nonzero setpoint
    "oscillating":       5,   # ADC variance too high during settle
    "convergence_fail":  4,   # ADC didn't settle within tolerance
    "timeout":           4,   # I2C read took too long
    "clamp":             1,   # setpoint was clamped (informational, low weight)
}

HIGH_WATER_MARK = 20          # trigger alert and inhibit channel


@dataclass
class FaultTracker:
    """Accumulates weighted faults for one channel."""
    channel: int
    score: int = 0
    log: list[tuple[str, str]] = field(default_factory=list)

    def record(self, fault_type: str, detail: str = "") -> None:
        weight = FAULT_WEIGHTS.get(fault_type, 1)
        self.score += weight
        self.log.append((fault_type, detail))
        msg = f"CH{self.channel} fault [{fault_type}] +{weight} → score={self.score}"
        if detail:
            msg += f"  ({detail})"
        logging.warning(msg)
        if self.score >= HIGH_WATER_MARK:
            self._high_water_alert()

    def _high_water_alert(self) -> None:
        recent = ", ".join(f[0] for f in self.log[-5:])
        logging.error(
            f"⚠  CH{self.channel} HIGH-WATER-MARK reached "
            f"(score={self.score} ≥ {HIGH_WATER_MARK}). "
            f"Recent faults: [{recent}]. "
            f"Channel inhibited — inspect limb and call reset_faults() to clear."
        )

    def reset(self) -> None:
        self.score = 0
        self.log.clear()

    @property
    def inhibited(self) -> bool:
        return self.score >= HIGH_WATER_MARK


# ---------------------------------------------------------------------------
# AD5593R I2C driver
# ---------------------------------------------------------------------------

class AD5593R:
    """
    Minimal driver for the AD5593R 8-channel DAC/ADC.

    Pass any object with the smbus2 interface:
        bus = smbus2.SMBus(1)
        chip = AD5593R(bus)
    Or provide your own I2C wrapper with:
        write_i2c_block_data(addr, reg, [byte, byte])
        read_i2c_block_data(addr, reg, length) -> [byte, ...]
    """

    def __init__(self, i2c, addr: int = ADDR_DEFAULT):
        self._i2c = i2c
        self._addr = addr

    def configure(self) -> None:
        """
        One-time setup:
          - Enable internal 2.5 V reference
          - I/O 0-3 → DAC outputs (SET1..4)
          - I/O 4-7 → ADC inputs  (ADC1..4, reading op-amp outputs)
          - ADC range = 0..2*VREF (catches op-amp output up to VCC ≤ 5 V)
        """
        # EN_REF in PD_REF_CTRL (0x0B), bit 9
        self._wreg(_REG_PD_REF, 0x0200)
        time.sleep(0.005)   # reference settle
        self._wreg(_REG_DAC_CONFIG, 0x000F)   # I/O 0-3 as DAC
        self._wreg(_REG_ADC_CONFIG, 0x00F0)   # I/O 4-7 as ADC
        # GEN_CTRL: DAC_RANGE=0 (0..VREF), ADC_RANGE=1 (0..2*VREF)
        self._wreg(_REG_GEN_CTRL, 0x0004)

    def set_dac_mv(self, channel: int, millivolts: float) -> None:
        """
        Set DAC channel 0-3 to millivolts (0..2500, hardware-clamped to 300).
        Writes the 12-bit code with the DAC-write mode bit set.
        """
        mv = max(0.0, min(millivolts, MAX_SET_MV))
        code = int(round(mv / (VREF_V * 1000) * 4096))
        code = min(code, 4095)
        reg = 0x10 | (channel & 0x3)          # DAC write: 0x10..0x13
        word = 0x1000 | code                   # bit 12 = DAC write flag
        self._wreg(reg, word)

    def read_adc_counts(self, channel: int) -> int:
        """
        Read ADC for board channel 0-3 (maps to AD5593R I/O 4-7).
        Returns raw 12-bit code.
        """
        adc_ch = channel + 4
        # Select single channel in ADC sequence register
        self._wreg(0x02, 1 << adc_ch)
        time.sleep(0.002)   # conversion time
        # Result register: upper nibble = channel tag, lower 12 bits = data
        raw = self._rreg(_REG_ADC_READ)
        return raw & 0x0FFF

    def adc_to_volts(self, counts: int) -> float:
        return counts / ADC_MAX * ADC_RANGE_V

    # -- low-level I2C --

    def _wreg(self, reg: int, word: int) -> None:
        hi = (word >> 8) & 0xFF
        lo = word & 0xFF
        self._i2c.write_i2c_block_data(self._addr, reg, [hi, lo])

    def _rreg(self, reg: int) -> int:
        data = self._i2c.read_i2c_block_data(self._addr, reg, 2)
        return (data[0] << 8) | data[1]


# ---------------------------------------------------------------------------
# Limb driver  (one per channel)
# ---------------------------------------------------------------------------

class LimbDriver:
    """
    Controls one SMA muscle wire channel with three automatic guards:

      move(setpoint_mv)
        ├─ Guard 1: reject if this channel is inhibited (high-water reached)
        ├─ Guard 2: probe for limb presence → record "absent" if missing
        └─ Guard 3: validate ADC response after settling → record fault types

    Fault score accumulates across calls.  When it reaches HIGH_WATER_MARK
    an error is logged and the channel is locked until reset_faults().
    """

    def __init__(self, dac: AD5593R, channel: int):
        if not 0 <= channel <= 3:
            raise ValueError(f"channel must be 0-3, got {channel}")
        self._dac = dac
        self._ch = channel
        self._faults = FaultTracker(channel)
        self._last_setpoint_mv: float = 0.0

    # ------------------------------------------------------------------ public

    def is_present(self) -> bool:
        """
        Active presence probe: apply PROBE_SET_MV, read ADC, zero out.
        Returns True if a limb is detected (ADC came off the rail).

        When no wire is attached the current-control loop is unsatisfied —
        the op-amp output rails near VCC, which the ADC reads as a high code.
        A connected wire pulls the op-amp to a mid-range output at equilibrium.
        """
        self._dac.set_dac_mv(self._ch, PROBE_SET_MV)
        time.sleep(PROBE_SETTLE_S)
        code = self._dac.read_adc_counts(self._ch)
        self._dac.set_dac_mv(self._ch, 0.0)

        if code >= ADC_RAILED_THRESHOLD:
            self._faults.record(
                "absent",
                f"ADC={code}/{ADC_MAX} ({self._dac.adc_to_volts(code):.2f} V) — railed"
            )
            return False

        log.debug(f"CH{self._ch} present: probe ADC={code} ({self._dac.adc_to_volts(code):.2f} V)")
        return True

    def move(
        self,
        setpoint_mv: float,
        settle_s: float = DEFAULT_SETTLE_S,
        validate: bool = True,
    ) -> bool:
        """
        Command a current setpoint (millivolts across 1 Ω sense → milliamps).
        Returns True if the move was accepted and the response looks valid.

        Args:
            setpoint_mv:  Target current in mA (≡ mV across 1R0 sense).
                          Hardware-clamped to [0, 300].
            settle_s:     Time to wait before reading ADC for validation.
            validate:     Set False to skip post-move ADC check (use sparingly).
        """
        # Guard 1 — channel inhibited by prior fault accumulation
        if self._faults.inhibited:
            log.error(
                f"CH{self._ch} move BLOCKED — channel inhibited "
                f"(fault score={self._faults.score}). Call reset_faults() to clear."
            )
            return False

        # Guard 2 — limb must be physically present
        if not self.is_present():
            log.error(f"CH{self._ch} move BLOCKED — limb not detected.")
            return False

        # Clamp setpoint
        clamped = max(0.0, min(setpoint_mv, MAX_SET_MV))
        if clamped != setpoint_mv:
            log.warning(
                f"CH{self._ch} setpoint {setpoint_mv:.1f} mV clamped to {clamped:.1f} mV"
            )
            self._faults.record("clamp", f"{setpoint_mv:.1f} → {clamped:.1f} mV")

        # Command
        self._dac.set_dac_mv(self._ch, clamped)
        self._last_setpoint_mv = clamped
        log.info(f"CH{self._ch} setpoint → {clamped:.1f} mV ({clamped:.0f} mA)")

        if not validate:
            return True

        time.sleep(settle_s)
        return self._validate_response(clamped)

    def stop(self) -> None:
        """Immediately zero the setpoint."""
        self._dac.set_dac_mv(self._ch, 0.0)
        self._last_setpoint_mv = 0.0
        log.info(f"CH{self._ch} stopped (setpoint → 0)")

    def reset_faults(self) -> None:
        """Clear accumulated fault score. Call after inspecting and fixing the limb."""
        log.info(f"CH{self._ch} fault tracker reset (was score={self._faults.score})")
        self._faults.reset()

    @property
    def fault_score(self) -> int:
        return self._faults.score

    @property
    def inhibited(self) -> bool:
        return self._faults.inhibited

    @property
    def fault_log(self) -> list[tuple[str, str]]:
        return list(self._faults.log)

    # --------------------------------------------------------------- private

    def _validate_response(self, setpoint_mv: float) -> bool:
        """
        Read ADC and decide whether the limb's response is appropriate.

        Expected behaviour (schematic, README_2.txt §BRING-UP):
          - Nonzero setpoint + wire attached → op-amp settles at mid output;
            ADC reads a moderate value (NOT railed).
          - Setpoints above the saturation knee (~233 mV for 42 Ω wire + 10 V)
            cause the op-amp to rail — that's expected physics, not a fault.
          - Zero setpoint → FET off; ADC may read low (near floor).
          - Railed ADC at any sub-saturation setpoint → loop unsatisfied → fault.
        """
        code = self._dac.read_adc_counts(self._ch)
        v = self._dac.adc_to_volts(code)
        ok = True

        if setpoint_mv == 0.0:
            # At zero, the FET should be off.  A railed ADC here means the FET
            # isn't shutting down or the op-amp is stuck.
            if code >= ADC_RAILED_THRESHOLD:
                self._faults.record(
                    "no_response",
                    f"ADC railed ({code}) with zero setpoint — FET not off?"
                )
                ok = False
            elif code <= ADC_FLOOR_THRESHOLD:
                # Fully off: expected.
                log.debug(f"CH{self._ch} at rest: ADC={code} ({v:.2f} V) — OK")
        else:
            # Non-zero setpoint below the saturation knee → should NOT be railed
            if setpoint_mv < WIRE_SAT_MV and code >= ADC_RAILED_THRESHOLD:
                self._faults.record(
                    "no_response",
                    f"ADC railed ({code}/{ADC_MAX}, {v:.2f} V) at "
                    f"{setpoint_mv:.1f} mV set — loop unsatisfied"
                )
                ok = False
            # ADC stuck at floor with a nonzero setpoint — something's wrong
            elif code <= ADC_FLOOR_THRESHOLD:
                self._faults.record(
                    "convergence_fail",
                    f"ADC at floor ({code}) at {setpoint_mv:.1f} mV set"
                )
                ok = False
            else:
                log.debug(
                    f"CH{self._ch} response OK: set={setpoint_mv:.1f} mV, "
                    f"ADC={code} ({v:.3f} V)"
                )

        return ok

    def _check_oscillation(self, n: int = 8, interval_s: float = 0.01) -> bool:
        """
        Optional: poll ADC n times and flag excessive variance.
        Returns True if the signal looks stable.
        """
        samples = []
        for _ in range(n):
            samples.append(self._dac.read_adc_counts(self._ch))
            time.sleep(interval_s)
        mean = sum(samples) / n
        variance = sum((s - mean) ** 2 for s in samples) / n
        std = variance ** 0.5
        log.debug(f"CH{self._ch} oscillation check: std={std:.1f} counts")
        if std > 150:   # ~0.18 V — tunable
            self._faults.record(
                "oscillating",
                f"ADC std={std:.0f} counts over {n} samples"
            )
            return False
        return True


# ---------------------------------------------------------------------------
# Convenience: build all four channels
# ---------------------------------------------------------------------------

def make_drivers(i2c, addr: int = ADDR_DEFAULT) -> tuple[AD5593R, list[LimbDriver]]:
    """
    Configure the AD5593R and return (chip, [ch0, ch1, ch2, ch3]).

    Usage:
        import smbus2
        bus = smbus2.SMBus(1)
        chip, limbs = make_drivers(bus)
        limbs[0].move(100)   # 100 mA on channel 0
    """
    chip = AD5593R(i2c, addr)
    chip.configure()
    drivers = [LimbDriver(chip, ch) for ch in range(NUM_CHANNELS)]
    return chip, drivers


# ---------------------------------------------------------------------------
# Quick self-test (run directly: python limb_driver.py)
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(levelname)-8s %(message)s"
    )

    # Demonstrate fault accumulation without real hardware
    log.info("=== Fault tracker demo ===")
    tracker = FaultTracker(channel=0)
    tracker.record("absent", "probe ADC=4090")
    tracker.record("absent", "probe ADC=4088")
    tracker.record("no_response", "ADC railed at 100 mV set")
    # Score is now 10+10+5 = 25 → alert fires at HIGH_WATER_MARK=20

    log.info(f"Inhibited: {tracker.inhibited}")
    tracker.reset()
    log.info(f"After reset — score={tracker.score}, inhibited={tracker.inhibited}")
