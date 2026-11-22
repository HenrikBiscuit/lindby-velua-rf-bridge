# Fit-test prints

Small test pieces to print before the full base (about 2-3 hours of printing instead of 10+ for
the whole box). Print them with the same printer, filament, nozzle and slicer profile you'll use
for the real parts, because tolerances depend on the printer.

Pieces 03-08 are cut straight out of the real model (folder *Fit-test coupons* in
`radio_enclosure.sc`), so they have exactly the geometry of the full print and update with it.

## 1. Gauges

| File | What to test | Pass when |
|---|---|---|
| `tp_01_tolerance_gauge.stl` | **INS** row: melt an M2 insert into 3.0 / 3.2 / 3.4 and an M3 insert into 3.8 / 4.0 / 4.2 | The insert goes in straight with light pressure and doesn't spin when you tighten a screw. The model uses **3.2 (M2)** and **4.0 (M3)**. |
| | **CLR** row: push an M2 screw through 2.2 / 2.4 / 2.6, an M3 through 3.2 / 3.4 / 3.6 | The screw passes freely without wobble. The model uses **2.4 (M2)** and **3.4 (M3)**. |
| | **PEG** row: put the CC2500's mounting hole and the PD board's 3V3 hole over the 1.7 / 1.8 / 1.9 pegs | The board slips on without force and doesn't rattle. The model uses **1.8** (CC2500) and **1.9** (PD board). |
| `tp_02_sma_hole_gauge.stl` | Push the CC1101 SMA thread and the IPEX-to-SMA bulkhead through 6.4 / 6.6 / 6.8 / 7.0 | The thread passes without binding and the nut tightens flat. The model uses **6.6** for both. |

## 2. Real-geometry pieces

`tp_real_geometry_plate.3mf` has 03, 04, 05 and 07 on one plate.

| File | Test with | Check |
|---|---|---|
| `tp_03_pd_usbc_corner.stl` | PD trigger board, M2 insert, M2x6 screw, a USB-C cable | The board drops into the pocket, the 3V3 hole goes over the peg, the GND screw lines up with the insert and the cable plugs fully in through the wall |
| `tp_04_cc2500_pad.stl` | CC2500 module, M2 insert, M2x6 screw | The module sits in the pocket with room for the soldered wires, the peg and screw line up with its two holes |
| `tp_05_cc1101_mount.stl` | CC1101 module, 2x M3 inserts, M3x12 screws | Both board holes line up with the inserts and the SMA sits centred in the wall hole. Only the top of the pedestal is included, so the screw tips poke out of the bottom. |
| `tp_06_bulkhead_wall.stl` | IPEX-to-SMA bulkhead | The bulkhead fits the 2 mm wall and the nut tightens |
| `tp_07_esp32_frame.stl` | Both ESP32-S3-DevKitC-1 boards | Lay the frame on the table and drop each board in. It should fit snugly between the stubs and not slide end to end. |
| `tp_08a_lid_corner_base.stl` + `tp_08b_lid_corner_lid.stl` | M3 insert, M3x12 screw | The lid lip slides into the wall corner without force or rattle, and the screw reaches the insert |

## Print orientation

- 01, 03, 04, 07, 08a: as exported, flat bottom down.
- 02: flat (it's a 2 mm plate).
- 05, 06: upright as exported. The SMA holes have a 45° teardrop top, so printing them standing
  like the real wall also shows whether the teardrop prints cleanly.
- 08b: flipped, lid top face down like the real lid.

## What I ended up with

Everything in the model is already set to the values that passed on my printer: M2 insert 3.2,
M3 insert 4.0, M2 clearance 2.4, M3 clearance 3.4, SMA holes 6.6, lid lip 0.3 mm per side,
ESP32 stubs 0.0 mm. If a piece doesn't fit on your printer, change the matching parameter in
`radio_enclosure.sc` (`fit`, `clr`, `pd_*`, `cc11_*`, `ant_z`, `ped_h`, ...) and reprint just that piece.
