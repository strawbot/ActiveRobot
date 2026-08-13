# v2 Board — Freerouting Handoff (4-layer)

**Board:** `muscle_wire_driver_v2_4L.kicad_pcb` — 63 × 70 mm, **4-layer**, 4 limb drivers
with Nano R4 / SEN0142 IMU / Makeblock 13030 plug-in boards straddling the limb rows.

**Stackup:**

| Layer | Assignment |
|-------|------------|
| F.Cu (top) | signal |
| In1.Cu | **GND plane** (solid pour) |
| In2.Cu | **+5V plane** (solid pour) |
| B.Cu (bottom) | signal |

+10V is distributed on the top layer as a wide rail (it's only 22 pads and the drive
current wants a fat trace, not a plane). GND (82 pads) and +5V (42 pads) live on the two
inner planes, which is what frees the top and bottom layers for signal routing.

**Status:** Placement is complete and verified — all footprints, all 665 nets, correct
pinouts, planes defined, placement clearance clean (≥0.2 mm). No signal tracks yet, so
Freerouting starts from a clean ratsnest.

A test auto-route confirmed the board **routes on 4 layers** — the two inner planes pull
all the ground/power copper off the outer layers, and the previously-stuck signals fit.
Freerouting will finish the job cleanly, including the power/ground **via fanout to the
inner planes**, which it does natively (my own router doesn't).

---

## Steps

1. **Open** `muscle_wire_driver_v2_4L.kicad_pcb` in KiCad 8 (Pcbnew).

2. **Confirm the stackup** — *File → Board Setup → Board Stackup → Physical Stackup*.
   You should see 4 copper layers. A standard 1.6 mm 4-layer build is fine
   (e.g. 0.035 mm copper, ~0.48 mm / 0.6 mm / 0.48 mm dielectrics — JLCPCB's default
   4-layer stackup works).

3. **Set design rules** — *File → Board Setup → Design Rules → Constraints* / *Net Classes*:

   | Rule | Value |
   |------|-------|
   | Min track width | 0.25 mm |
   | Min clearance | 0.20 mm |
   | Via diameter / drill | 0.60 / 0.30 mm |
   | Power/`+10V`/`*_DRAIN*` track | 0.40–0.50 mm |

4. **Export DSN** — *File → Export → Specctra DSN…* → `muscle_wire_driver_v2_4L.dsn`.
   KiCad includes the plane layers and their nets, so Freerouting knows to via-drop
   GND to In1.Cu and +5V to In2.Cu.

5. **Run Freerouting** on the `.dsn`, autoroute + a few optimization passes, export the
   `.ses`.

6. **Import** — *File → Import → Specctra Session…* → select the `.ses`.

7. **Re-pour the zones** — *Edit → Fill All Zones* (hotkey `B`) so the GND and +5V planes
   fill around the vias and traces. Then run **DRC**; it should be clean at 0.20 mm.

---

## Keep-outs / gotchas

- **Makeblock antenna:** the antenna end overhangs the right board edge — add a copper
  **keep-out zone on all 4 layers** over the antenna footprint before exporting the DSN,
  so nothing (including the planes) routes under it.
- **Plane fanout:** every GND and +5V SMD pad needs a short trace + via down to its plane.
  Freerouting does this automatically; you don't place them by hand.
- **+10V rail:** already assigned to the top layer; give it a wide net-class (0.5 mm).
- **Plug-in headers** (Nano / IMU / Makeblock) are THT and are already in the netlist —
  their GND/+5V pins connect straight to the planes since through-hole pads span all
  layers.

---

## Files in this folder

- `muscle_wire_driver_v2_4L.kicad_pcb` — **use this**: verified 4-layer placement, the
  Freerouting input.
- `muscle_wire_driver_v2_4L_routed.kicad_pcb` — my best-effort auto-route (≈95 % done);
  reference only, useful to see how the signal layers lay out. Not the file to route from.
- `muscle_wire_driver_4limb_v2.kicad_pcb` — the complete, fully-routed **2-layer, 70×88**
  fallback board if you ever want a finished-today option at the larger size.
