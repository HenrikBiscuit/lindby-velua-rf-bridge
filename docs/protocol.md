# Protocol notes

All of this was measured on my own remote (PCB marked YM-2.4G-B, 8 buttons, one unmarked SOP16
chip, 16 MHz crystal) and my lamp (Lindby Velua, Lampenwelt art. 10041966). Other remotes might
use a different ID. I think the constant `0D BF 29` bytes are the remote ID.

## Radio

| Parameter | Value |
|---|---|
| Modulation | 2-FSK, wide deviation (around ±250 kHz) |
| Data rate | 250 kbps (4 µs per bit) |
| Channels | 2402.00, 2448.00 and 2478.95 MHz (nRF24 channels 2, 48 and 79) |
| Hopping | 3 packets per channel, then the next channel. A full round takes about 11.69 ms |
| Packet spacing | about 1245 µs on one channel |
| CRC / whitening | none on the radio level, the frame is sent as is |

## Packet

```
 AA    CC CC CC CC    B9 A1 99    0D BF 29 | c0 c1 | a0 a1 a2 a3 | x0 x1 | t0 t1 t2 t3 t4 t5
 pre-  sync lead-in   sync        17-byte frame
 amble (1100 x8)
```

- `AA` (10101010) is the preamble, and the lamp needs it. The nRF24 and the CC2500 in packet
  mode both strip it, so for a long time my transmitter sent `1001100` + `1100` x8 instead.
  With that the lamp only reacted to about 30% of presses. With `AA` in front it reacts every
  time. I send `AA AA AA AA`, a longer preamble doesn't hurt.
- `CC CC CC CC B9 A1 99` never changes. The nRF24 can use `B9 A1 99` as a 3-byte address.
- Nothing fixed comes after the frame, so there is no trailer.

### Frame

| Bytes | Meaning |
|---|---|
| 0-2 `0D BF 29` | constant (probably the remote ID) |
| 3-4 | counter (8 states, see below) |
| 5-8 | action: which button, and press or release |
| 9-10 | `A1 B6` for most buttons (different for buttons 6 and 8) |
| 11-16 | check bytes, a linear (GF(2)) function of the rest |

The frame is linear over GF(2). If you XOR three valid frames together you get another valid
frame. That's how I could generate every button at every counter state from a few captures.

## Counter

The remote has one counter shared by all buttons. It goes through 8 states in this order and
then starts over:

```
release counter:  B922, A832, A911, B801, 8A02, 9B12, 9A31, 8B21
press counter:    8B30, B933, A823, A900, B810, 8A13, 9B03, 9A20
```

It looks like a rolling code, but the lamp doesn't check it. Sending the same state three
times toggles the lamp three times, so every press + release counts as one action no matter
what the counter is. Because of this you shouldn't repeat a command for reliability, since each
repeat is another toggle.

## Button presses

A press looks like this on air:

```
press frame,   repeated for about 100 ms on a tap, or for as long as the button is held
release frame, repeated for about 1.6 s
```

- The release frame is the same for all 8 buttons.
- The press frame depends on the button. For button *b* at counter state *s*:

  `press[b][s] = press_onoff[s] XOR BTN_XOR[b]`

- A hold is the same press frame repeated for as long as the button is held, followed by the
  normal release. Brightness and colour temperature keep changing while the press frame is
  being sent.

### Release frames (same for all buttons)

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

The numbers are the order I recorded the buttons in, and they are also the keys 1-8 in the
transmitter. On the remote that is the left column from top to bottom (1-4), then the right
column from top to bottom (5-8). `BTN_XOR` came out the same for every counter state I captured (3 to 5 per button).

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

## Other things I noticed

- On each channel, the second packet (1.25 ms after the first) is garbled from byte 9 onwards.
  I saw this on both the nRF24 and the CC2500, and it's about half of all packets. The lamp works
  fine without me copying this, so I think it's just a quirk in the remote's firmware.
- The lamp never transmits anything, so Home Assistant can't know if it's on or off.
