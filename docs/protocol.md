# Lindby Velua remote — protocol notes

Everything below was measured on one remote (PCB marking **YM-2.4G-B**, 8 buttons, one
unmarked SOP16 chip, 16 MHz crystal) and one lamp (Lindby Velua, Lampenwelt art. 10041966).
Other remotes may use a different ID (the constant `0D BF 29` bytes are probably the remote ID).

## RF layer

| Parameter | Value |
|---|---|
| Modulation | 2-FSK, wide deviation (≈ ±250 kHz) |
| Data rate | 250 kbps (4 µs per bit) |
| Channels | 2402.00, 2448.00, 2478.95 MHz (nRF24 channels 2 / 48 / 79) |
| Hopping | 3 packets per channel, then hop; full cycle ≈ 11.69 ms |
| Packet spacing | ≈ 1245 µs on one channel |
| CRC / whitening | none at the RF layer (the frame is used as-is) |

## Packet on air (byte-aligned)

```
 AA    CC CC CC CC    B9 A1 99    0D BF 29 | c0 c1 | a0 a1 a2 a3 | x0 x1 | t0 t1 t2 t3 t4 t5
 ───   ───────────    ────────    ───────────────────────────────────────────────────────
 pre-  sync lead-in   sync        17-byte frame
 amble (1100 ×8)
```

- **`AA` (10101010) is the preamble — and it is essential.** Every digital receiver used during
  the investigation (nRF24, CC2500 in packet mode) strips it, so for a long time the transmitter
  sent `1001100` + `1100×8` instead. The lamp then decodes only ~30 % of taps. With `AA` in
  front, it decodes every tap. My transmitter sends `AA AA AA AA` (a longer preamble is harmless).
- `CC CC CC CC B9 A1 99` is constant. The nRF24 can lock on `B9 A1 99` as a 3-byte address.
- Nothing fixed follows the frame (no trailer).

### Frame fields

| Bytes | Meaning |
|---|---|
| 0–2 `0D BF 29` | constant (remote ID?) |
| 3–4 | counter (8-state cycle, see below) |
| 5–8 | action: which button, and press vs. release |
| 9–10 | `A1 B6` for most buttons (differs for buttons 6 and 8) |
| 11–16 | check bytes — a linear (GF(2)) function of the other fields |

The frame is **linear over GF(2)**: XOR-ing two valid frames and a third valid frame gives a
fourth valid frame. That is what made it possible to synthesise every button at every counter
state from a handful of captures.

## Counter: an 8-state cycle that the lamp ignores

The remote steps one global counter (shared by all buttons) through 8 states, in this order,
wrapping around:

```
release counter:  B922 → A832 → A911 → B801 → 8A02 → 9B12 → 9A31 → 8B21 → …
press counter:    8B30 → B933 → A823 → A900 → B810 → 8A13 → 9B03 → 9A20 → …
```

It looks like a rolling code, but **the lamp does not check it**: sending the same state three
times toggles the lamp three times. Each press+release = one action, whatever the counter is.
(Consequence: never "repeat for reliability" — every repeat is another toggle.)

## A button press

```
 press frame  ×  (≈100 ms for a tap, or as long as the button is held)
 release frame ×  ≈1.6 s
```

- **Release frame — the same for all 8 buttons** (it just says "button up").
- **Press frame — button-specific.** For button *b* at counter state *s*:

  `press[b][s] = press_onoff[s] XOR BTN_XOR[b]`

- **Hold** = the same press frame, repeated for as long as you hold the button, then the
  normal release. Brightness and colour temperature ramp while the press frame keeps coming.

### Release frames (all buttons)

| # | counter | frame |
|---|---|---|
| 0 | B922 | `0D BF 29 B9 22 57 5E 4B 9C A1 B6 ED 99 1E A7 3E AC` |
| 1 | A832 | `0D BF 29 A8 32 DF 5E 4B 9C A1 B6 FC 89 A0 18 B2 F1` |
| 2 | A911 | `0D BF 29 A9 11 57 5E 4B 9C A1 B6 FD AA A1 DA 63 B4` |
| 3 | B801 | `0D BF 29 B8 01 DF 5E 4B 9C A1 B6 EC BA 1F 65 EF E9` |
| 4 | 8A02 | `0D BF 29 8A 02 57 5E 4B 9C A1 B6 DE B9 C3 26 40 46` |
| 5 | 9B12 | `0D BF 29 9B 12 DF 5E 4B 9C A1 B6 CF A9 7D 99 CC 1B` |
| 6 | 9A31 | `0D BF 29 9A 31 57 5E 4B 9C A1 B6 CE 8A 7C 5B 1D 5E` |
| 7 | 8B21 | `0D BF 29 8B 21 DF 5E 4B 9C A1 B6 DF 9A C2 E4 91 03` |

### Press frames, button 1 (On/off)

| # | counter | frame |
|---|---|---|
| 0 | 8B30 | `0D BF 29 8B 30 5F C7 CB 14 A1 B6 DF 9A 4D DD 6B 2D` |
| 1 | B933 | `0D BF 29 B9 33 D7 C7 CB 14 A1 B6 ED 99 91 9E C4 82` |
| 2 | A823 | `0D BF 29 A8 23 5F C7 CB 14 A1 B6 FC 89 2F 21 48 DF` |
| 3 | A900 | `0D BF 29 A9 00 D7 C7 CB 14 A1 B6 FD AA 2E E3 99 9A` |
| 4 | B810 | `0D BF 29 B8 10 5F C7 CB 14 A1 B6 EC BA 90 5C 15 C7` |
| 5 | 8A13 | `0D BF 29 8A 13 D7 C7 CB 14 A1 B6 DE B9 4C 1F BA 68` |
| 6 | 9B03 | `0D BF 29 9B 03 5F C7 CB 14 A1 B6 CF A9 F2 A0 36 35` |
| 7 | 9A20 | `0D BF 29 9A 20 D7 C7 CB 14 A1 B6 CE 8A F3 62 E7 70` |

Press state *i* pairs with release state *i*.

### Button table

Button numbers are the order I recorded them in (they are also the transmitter's keys 1–8). `BTN_XOR` is constant
across all counter states (verified on 3–5 captured states per button).

| # | Function | `BTN_XOR` (17 bytes) |
|---|---|---|
| 1 | On/off | `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| 2 | Night (jump to lowest brightness) | `00 00 00 00 00 10 22 80 88 00 00 10 22 49 97 EA 0C` |
| 3 | Brightness up | `00 00 00 00 00 20 77 80 88 00 00 20 77 94 16 19 89` |
| 4 | Brightness down | `00 00 00 00 00 33 31 80 88 00 00 33 31 B7 14 DA A9` |
| 5 | Cycle 3 colour temperatures | `00 00 00 00 00 40 DD 80 88 00 00 40 DD C0 44 AF 4E` |
| 6 | Timer 60 s | `00 00 00 00 00 00 11 80 99 80 88 00 00 40 DD 5C 3B` |
| 7 | Colour temperature cooler | `00 00 00 00 00 00 11 90 BB 00 00 10 22 68 D2 24 3B` |
| 8 | Colour temperature warmer | `00 00 00 00 00 00 11 91 98 88 00 11 01 5D E8 8B 94` |

## Quirks seen but not needed

- Within each channel visit, the packet that follows 1.25 ms after the first is garbled from
  byte 9 onward (seen on both the nRF24 and the CC2500 — about half of all packets). The lamp
  works fine without reproducing this, so it is probably a quirk of the remote's firmware.
- The lamp has no state feedback: it never transmits, so Home Assistant cannot know whether
  it is on or off.
