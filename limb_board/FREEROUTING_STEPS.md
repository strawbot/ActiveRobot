# Corner board — final routing state & Freerouting finish

**Board:** `muscle_wire_driver_corner_4L.kicad_pcb` — 108 × 96 mm, 4-layer
(F.Cu / In1 = GND plane / In2 = +5V plane / B.Cu).

## What is already routed in the board
- **GND / +5V** — inner planes, every power pad tied via-in-pad (island-safe).
- **AD5593R fanout** — all 52 fine-pitch pins escaped to vias at open pitch.
- **84 of the 92 per-channel cluster-internal nets** — GATE / SNS / OUT / FB / DRAIN,
  routed and clearance-clean (0 violations across 725 segments). These are the dense
  local runs that jammed every autorouter; the cluster was re-laid so each net is a
  near-straight shot, and they route deterministically now.

## What is left for Freerouting (small, easy)
- **SET / ADC** — AD5593R escape-via → op-amp / channel (open grid).
- **Global bus** — SDA, SCL, SEL1-4, IMU_INT, BT_TX, BT_RX, +10V.
- **8 support nets** — L1..L4 RESET and VREF (2-pad each; boxed by the U1 support
  column, easiest to drop by hand).

## Steps
1. Open in KiCad → **File ▸ Export ▸ Specctra DSN** → `corner.dsn`.
   (The pre-routed traces export as existing copper; Freerouting builds around them.)
2. Freerouting: open `corner.dsn`. **Autoroute layers = F.Cu + B.Cu only** (In1/In2 are
   planes — leave them unchecked). Keep automatic neckdown on.
3. **Autoroute.** With the clusters already done and the pins pre-escaped, the incompletes
   should fall to a small number quickly.
4. If a handful remain (expect the RESET/VREF and maybe a few bus nets), finish them in
   KiCad's interactive router (hotkey `X`, push-and-shove) — they're short 2-pad runs.
5. **File ▸ Export Specctra Session (.ses)** → import back in KiCad → run **DRC** → send
   me the report.

## Notes
- Set **Rules ▸ Clearance Matrix** default = 0.2 mm to match the board (kills stray
  clearance violations).
- GND/+5V shown as "incompletes" are plane nets — ignore, or enable
  *Autoroute ▸ ignore conduction areas*.
