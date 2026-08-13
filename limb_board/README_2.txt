4-CHANNEL MUSCLE WIRE (SMA) DRIVER — AD5593R + MCP6002 + IRLML6344
Schematic: muscle_wire_driver.kicad_sch (KiCad 8, single A3 sheet, label-based wiring)

TOPOLOGY (per channel i = 1..4)
  AD5593R I/O(i-1) [DAC, net SETi] -> MCP6002 IN+
  MCP6002 IN- <- 1k (Ri1) <- SNSi (top of 1R0 sense resistor Ri5)
  10n (Ci1) from op-amp OUT to IN-  (dominant-pole compensation)
  OUT -> 220R (Ri2) -> GATEi -> Qi gate;  100k (Ri3) gate pulldown
  OUT -> 10k (Ri4) -> ADCi -> AD5593R I/O(3+i) [ADC]  (op-amp output telemetry)
  Qi drain -> DRAINi -> J2-i;  SS14 (Di) from DRAINi (A) to +10V (K)
  Qi source -> SNSi -> 1R0 -> GND
  Current law: I = VSETi / 1.0R.  300 mA max  =>  VSET <= 300 mV (DAC codes 0..491).

CONNECTORS
  J1 (6-pos terminal block): 1 GND, 2 VCC (3.2-5.0 V), 3 +10V (muscle wire supply),
                             4 SDA, 5 SCL, 6 A0 (select / in-use LED)
  J2 (4-pos terminal block): muscle wire returns to DRAIN1..4
  Load path: +10V -> muscle wire -> DRAINx.

AD5593R SETUP (TSSOP-16, AD5593RBRUZ)
  - Enable internal 2.5 V reference: EN_REF=1 in PD_REF_CTRL (0x0B).
  - DAC_CONFIG (0x05) = 0x000F (I/O0-3 as DACs).
  - ADC_CONFIG (0x04) = 0x00F0 (I/O4-7 as ADCs).
  - GEN_CTRL_REG (0x03): DAC_RANGE=0 (0..VREF), ADC_RANGE=1 (0..2xVREF)
    so op-amp outputs up to VCC read correctly at 5 V supplies.
  - I2C address 0x10 (A0 low) / 0x11 (A0 high). Datasheet fixed bits: 001000x.
  - RESET pulled to VCC via R3 10k. Power-up pin state = 85k pulldowns (FETs off). 

BOM SUMMARY
  U1   AD5593RBRUZ            TSSOP-16
  U2,3 MCP6002-I/SN           SOIC-8
  Q1-4 IRLML6344TRPBF         SOT-23      (30 V, ~37 mR @ 2.5 Vgs)
  D1-4 B5819W                 SOD-123 (low profile for straw fit)
  D5   green LED              0603
  R11/21/31/41  1k    0603    feedback
  R12/22/32/42  220R  0603    gate series
  R13/23/33/43  100k  0603    gate pulldown
  R14/24/34/44  10k   0603    ADC series protection
  R15/25/35/45  1R0   1206    sense, 90 mW @ 300 mA
  R1,R2 4.7k 0603 (I2C pullups)  R3 10k (RESET)  R4 10k (A0 pulldown)  R5 2.2k (LED)
  C1,C3,C4,C5 100n 0603;  C2 10u 0805 (VCC);  C6 10u 0805 (+10V);  C11-41 10n 0603
  J1 SM06B-SRSS-TB JST SH 6P (input);  J2 SM04B-SRSS-TB JST SH 4P (actuator end);
  J3 = 2 aux solder pads paralleling +10V/GND for full 4-channel simultaneous load

FORM FACTOR (10 mm straw)
  - 8 mm x ~70 mm strip, 0.8 mm PCB, all parts on one side.
  - Clearance: sqrt(8^2 + 2.6^2) = 8.4 mm diagonal < 9.5 mm usable bore.
  - JST SH connectors at both ends (8.0 mm wide, 2.9 mm tall - diagonal 8.8 mm, fits).
  - SH contacts are 1 A each: if all 4 channels can be on at once (1.2 A), bring
    +10V/GND in through the J3 solder pads and use J1 for logic only.
  - THERMAL (measured wire R = 42R cold):
    * Max loop current at 10 V = 10/(42+1) = ~233 mA cold, ~260 mA hot
      (nitinol R drops through transition). 300 mA needs ~13.5 V supply.
    * FET dissipation = I*(10 - 43*I): peaks at 0.58 W @ 116 mA held setpoint,
      ~0 W at full-on or off. Only sustained mid-range holds generate heat.
    * Spec 2 oz copper; flood drain pads; via-stitch to a full back-side
      copper spreader -> est. 100-130 C/W, 60-75 C rise worst case. OK vs
      150 C junction limit, but warm to touch through the straw.
    * Firmware option "PWM assist": alternate setpoint 0 <-> ~230 mA at
      10-50 Hz for the same average current; SMA thermal time constant
      (seconds) smooths it and FET dissipation drops to ~nothing.
    * OPERATING CONSTRAINT: wires form two opposing (antagonistic) pairs;
      at most 2 channels active at once (one per pair). Worst-case board
      dissipation = 2 x 0.58 W = 1.16 W. J1 +10V pin sees <= ~0.47 A,
      within the SH 1 A/pin rating; J3 aux pads are optional insurance.
    * Layout: make CH1/CH2 one pair and CH3/CH4 the other, placed in
      channel order along the strip, so simultaneously-active FETs are
      usually non-adjacent.
    * Firmware interlock: never allow nonzero setpoints on both channels
      of a pair; check the antagonist's ADC reading before actuating.

LAYOUT NOTES (for the PCB step)
  - 10u + 100n right at AD5593R VDD (datasheet requirement); 100n at VREF pin.
  - Solid GND pour; return sense-resistor grounds directly to the pour.
  - Keep +10V/drain loops away from VREF cap and SETx traces.
  - 20-30 mil for drain traces (300 mA).

BRING-UP
  1. Power VCC only. Check I2C ACK at 0x10; toggle A0 -> 0x11 and LED lights.
  2. Enable EN_REF; measure 2.500 V +/- 5 mV on VREF (C3).
  3. Configure DAC/ADC pins; sweep SET1 0..300 mV with no wire attached:
     op-amp output should rail near VCC (loop unsatisfied) - confirms polarity.
     NOTE: with 42R wires, setpoints above ~233 mV saturate the loop; the upper
     inflection of the familiar ADC curve is the op-amp railing at that limit.
  4. Attach a wire on CH1, sweep setpoint, verify I = VSET/1R0 with a DMM and
     that the ADC1 curve shows your familiar two-inflection shape.
