# Strawbot 4×2 — v2 Upgrade List

Running list of improvements for the next board revision.
The v1 order is committed and unaffected by anything here.

---

## ⚠ Must-fix bugs (found in v1)
- **Arduino Nano R4 footprint is left–right mirrored.** The socket pinout is a mirror of the real Nano board, so on v1 the Nano must be plugged in **upside down** to work. Un-mirror the footprint (correct the pin mapping/side) so it seats normally, and re-verify against the Nano R4 pinout. *(Confirmed on the bench while prototyping I²C.)*
  - **v1 workaround A:** plug the Nano in belly-up (component side toward the board) on the top-side headers.
  - **v1 workaround B (cleaner):** mount the socket on the **bottom** of the board and insert the Nano from below — opposite-side access reverses handedness and cancels the mirror, keeping the Nano right-way-up. USB + cable then hang off the bottom (check clearance); silkscreen is on the wrong face (cosmetic).
  - **Before soldering all 5:** continuity-check one board (or eyeball pin 1 vs the Nano R4 pinout) to confirm it's a pure left-right mirror and not a row swap.
- **While at it, re-check every plug-in module footprint** (IMU/SEN0142, both Makeblock headers) against its real pinout for the same mirror/orientation error before committing v2.

---

## 1. Standardize all four limbs on the L2 layout
**What:** Make L1 / L3 / L4's DAC + ADC placement and orientation match L2 (rotated as a block to each corner).

**Why:** L2 routes with the least bottom-layer copper — **89.8 mm B.Cu vs L3's 129.5 mm** (L1 125.2, L4 96.1). Today the limbs disagree: DAC rotations 90/90/0/0, ADC rotations 0/180/−90/−90, so each limb routes differently and the awkward ones shove nets to the bottom layer (more blue, more vias).

**Benefit:** fewer bottom traces + vias, consistent assembly/inspection, simpler firmware reasoning.

---

## 2. Switch to the vertical (top-entry) connector to shrink board width
**What:** Use the top-entry **BM05B-SRSS** (narrow footprint) instead of the side-entry **SM05B-SRSS** (body extends horizontally → wider land pattern).

**Why:** The vertical footprint is narrower → recover board width.

**Caveats:**
- Cables then plug in from **above** — check clearance against the Makeblock overhang and overall mechanical fit before committing.
- **Make the footprint name, BOM part, and 3D model all agree this time.** The v1 mismatch came from a "BM05B…Vertical" name sitting on side-entry pads. Top-entry parts: BM05B-SRSS-TB = C160391 (low stock) / BM05B-SRSS-TBT = C265092 (well stocked).

---

## 3. Put the MPU6050 chip directly on the board (drop the IMU module)
**What:** Replace the socketed SEN0142 / GY-521 module (J6) with the bare **MPU6050 QFN** chip + its support parts.

**Why:** Removes the module and its 1×5 socket → recover board length.

**Caveats (important):**
- The MPU6050 is a **3.3 V** part. The GY-521/SEN0142 module hides an onboard **3.3 V regulator and I²C level shifting**. On the bare chip you must supply 3.3 V (add an LDO) and handle SDA/SCL between the Nano's 5 V and the chip's 3.3 V — run the I²C bus at 3.3 V or add a level shifter.
- Add the chip's decoupling (VDD / VLOGIC / REGOUT caps), the AD0 address strap, and the QFN-24 (4×4 mm) footprint. Slightly harder assembly (QFN) but much smaller.

---

## 4. Explore a 90° reorientation of each limb (connectors on top/bottom edges)
**What:** Rotate each limb block 90°. Connectors move to the **top and bottom edges** of the board; the limb circuits run **vertically**; the **DAC/ADC sit along the horizontal**; caps and LEDs move to suit.

**Why / to explore:**
- Cables then exit the top and bottom edges instead of the sides — evaluate against where the limbs physically attach to the robot.
- DAC/ADC laid horizontally may line the I²C bus up more directly toward the central spine → potentially cleaner routing.
- Natural moment to rebuild all four limbs *identically* from one clean template (folds in with #1).

**Considerations:**
- Re-check the width/length trade against the connector choice (#2): top/bottom connectors + a narrower vertical connector shift the board's aspect ratio.
- Keep the spine + IMU centered (weight distribution) as in v1.
- This is a from-scratch limb re-layout and full re-route, so do it together with #1 and #2 rather than piecemeal.

---

## Also on the radar (found during v1)
- **Connector placement:** remove the 1-pin CPL nudge and align to a JLC-stocked part so SMT passes without a manual review.
- **ADS1015 ALERT/RDY (pin 2):** optionally route to a Nano GPIO + pull-up for interrupt-driven reads instead of polling.
- **+10V common (connector pin 3):** carries the sum of all channels firing — if driving several at once, give it a dedicated heavier feed or more contacts to stay under ~1 A/contact.
- **Via sizes:** unify to a single via size (the two-size set crashed KiCad's .ses importer in v1).

---

## Firmware note — offloading ADS1015 reads (RA4M1 / R7FA4M1AB3CFM)
Idea: use ALERT/RDY (ADS1015 pin 2) to drive the I²C reads by DMA with little/no ISR.

- **The MCU has the pieces:** ELC (event linking), DTC (transfer-on-trigger, no ISR), 4-ch DMAC, RIIC (I²C). A pin edge → external IRQ → DTC can move the 2 result bytes with **no read ISR** — both the trigger and the data move are CPU-free.
- **Blockers to going fully headless:**
  - RIIC has **no auto-start** — START / address / STOP is software-sequenced. Driving it with chained DMA register-writes is possible but fragile/research-grade.
  - ADS1015 **mux** → must rewrite the config register per channel (write-then-read ping-pong), not a fixed repeatable read.
- **Realistic build:** FSP `r_iic_master` driver with **DTC-backed transfers** (bytes moved by DTC, CPU only runs a short completion callback), or ALERT→IRQ→DTC read + a tiny kick for the next channel. Requires dropping Arduino `Wire` for the Renesas **FSP**.
- **Verdict:** worth it only if CPU headroom gets tight; for 2-byte reads the savings are marginal. Fully ISR-free 16-channel acquisition isn't practical on the RIIC. (The clean autonomous case needs continuous mode + one channel per chip — not the muxed design.)

---

## Firmware / safety note — I²C fault handling
Bus hangs are a real failure mode (9 devices on one bus, a moving/vibrating robot). Plan for all three:

- **Detection:** RIIC status/error flags (NACK, arbitration/bus error, built-in timeout) surfaced via the FSP error callback; **plus a software transaction timeout** (arm a timer on start — if no completion event by the deadline, declare a fault); **plus** reading the physical line state (SDA stuck low while idle = classic slave hang; RIIC exposes SDA/SCL input-monitor bits).
- **Recovery (I²C "bus clear", per NXP UM10204):** release SDA, pulse SCL up to **9 times** (checking SDA after each) until the stuck slave releases, issue a **STOP**, then reset/re-init the peripheral. RA4M1 RIIC has hardware assist (direct line control, extra-SCL-clock output, software-reset bit) — confirm exact register bits from the hardware manual. Whole sequence ≈ 100 µs @ 100 kHz → fits one sub-1 ms action.
- **Independent failsafe (important):** MCP4728 DACs **hold their last output with no refresh**, so a bus hang freezes every channel at its last commanded level — a stuck-ON muscle wire is a thermal risk. Add a hardware watchdog that, on a fault or lost heartbeat, **de-energizes the channels** (kill +10V or force gates off) regardless of the I²C bus state. This should be independent of the MCU's I²C path.

---

## Firmware note — labelled script storage in the 8 KB data flash
The "8K EEPROM" is the RA4M1's **data flash** (separate from the 256 KB code flash): flash, not true EEPROM → **erases in small blocks (~64 B, confirm in manual)**, ~**100k** erase/write cycles, **directly readable** (read = memcpy from its address), and writes/erases can run in **background (BGO)** with a completion interrupt → fits the never-block model.

**Approach: small log-structured key-value store** (append, never rewrite in place).

Record format appended to flash:
```
[valid 1B][label 16B null-padded][length 2B][data 20–500B][CRC16 2B]
```
- **In-RAM index** (label → latest record offset) built by scanning flash once at boot; <1 KB RAM for dozens of labels.
- **Read** = index lookup → memcpy payload from flash (non-blocking).
- **Write/update** = append new record + update index; old one is superseded ("latest wins").
- **Delete** = append a tombstone (zero length / delete flag).
- **Compaction** = ping-pong two 4 KB halves: fill the active half, then copy live records to the other half and erase the first. **Chunk the erase across scheduler ticks** so compaction never stalls an action.
- **Power-loss safe** via per-record CRC (half-written record fails CRC, ignored at boot).

**Capacity:** ~15 max-size (500 B) items, many more if short. Need more → external I²C EEPROM/FRAM later.

**Off-the-shelf alternatives:** LittleFS (one file per label; wear-leveling + power-loss safe, but metadata overhead eats into 8 KB) · R4 Arduino EEPROM lib as a byte-addressed base (add own append logic; its page-rewrite commit is blocking/wear-heavy).

---
*Add items here as they come up.*
