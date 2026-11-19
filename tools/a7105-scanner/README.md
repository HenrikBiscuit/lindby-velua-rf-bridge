# velua-a7105 — A7105 receiver / scanner (transmit board's sibling)

Brings up the **XL7105-SY-B** (AMICCOM **A7105**) module on the spare ESP32-S3 and tries to
receive the Lindby Velua remote *natively* — with the A7105's own preamble/ID-sync, CRC and
data-whitening, which the nRF24 and CC2500 could not reproduce.

Flash with **only this board plugged in**, or select its COM port in the PlatformIO port
picker (env `board_c`, 115200 baud).

---

## 1. Identify the pads (they are unmarked — do this first)

The module is the common **MD7105-SY / XL7105-SY** layout: **14 castellated pads, 7 per long
edge**. Logical pinout (AMICCOM/ElectroDragon reference):

| Pad | Signal | Pad | Signal |
|----:|--------|----:|--------|
| 1 | NC | 8 | NC |
| 2 | **GIO1** | 9 | **VDD (3.0–3.3 V)** |
| 3 | NC | 10 | **GND** |
| 4 | **GIO2** | 11 | NC |
| 5 | NC | 12 | **SCS** |
| 6 | NC | 13 | **SCK** |
| 7 | **GND** | 14 | **SDIO** |

Because your board has **no silkscreen on the pads**, confirm the four that matter before
powering it, with a multimeter in **continuity / beep** mode (module unpowered):

1. **GND** — the two mounting/ground areas and usually the **crystal's metal can** are GND.
   Find the pad(s) that beep against the crystal can. Those are pads **7 and 10**.
2. **VDD** — the pad that beeps to the **+ side of the small decoupling capacitors** near the
   chip, and does **not** beep to GND. That's pad **9**. (If unsure, measure resistance to
   GND: VDD reads a few kΩ or open, never 0 Ω.)
3. With GND at pad 7 and VDD at pad 9 located, the pad **next to VDD on the far side from the
   antenna** row gives you the SPI group **12/13/14 = SCS/SCK/SDIO** in order, and **GIO1 =
   pad 2**. If in doubt, buzz SCS/SCK/SDIO against the A7105 chip pins (they go straight to
   the QFN).

> ⚠️ **VDD is 3.3 V only — never 5 V.** Reversed power or 5 V will destroy the module.

If a pad's role is ever unclear, stop and tell me which pads beep to what and I'll help map it.

---

## 2. Wire to the spare ESP32-S3

| A7105 pad | Signal | ESP32-S3 GPIO |
|---|---|---|
| 9 | VDD | **3V3** |
| 7 or 10 | GND | **GND** |
| 12 | SCS | **GPIO10** |
| 13 | SCK | **GPIO12** |
| 14 | SDIO | **GPIO11** |
| 2 | GIO1 | **GPIO13** |
| 4 | GIO2 | *(leave unconnected)* |

Pins chosen to avoid the S3's reserved GPIOs (PSRAM 35–37, strapping 0/3/45/46, USB 19/20,
RGB 38/48). Keep the wires short — it's a 2.4 GHz SPI bus.

*(If your spare board is a classic ESP32, not an S3, these GPIO numbers also work, but GPIO12
is a boot strapping pin there — keep SDIO off GPIO12 and tell me, I'll remap.)*

---

## 3. What to expect on the monitor

```
A7105 SPI round-trip: wrote B9 A1 99 0D, read B9 A1 99 0D -> OK (wiring good)
  calibration: IF 0x22=0x.. (FBCF ok), VCO 0x25=0x.. (VBCF ok)
>> A7105 ID=B9 A1 99 0D  whiten=1 crc=1 fec=0  rate0x01  chan=sweep 2/48/79
```

**Milestone 1 — is it alive?** The *SPI round-trip* line must say **OK** and calibration must
say **ok/ok**. That alone proves the unmarked-pad wiring is right. Send me this output first.

**Milestone 2 — does it hear the remote?** With the remote tapped nearby, received packets
print as:

```
P <ms> ch<2|48|79> crcOK fecok 0DBF29....
```

`crcOK` means the A7105 accepted the packet under the current settings — and if whitening is
on, the printed payload is **de-whitened** (the real underlying bytes).

Live keys (type in the monitor): `i` next ID-code guess · `w` whitening on/off · `c` CRC
on/off · `f` FEC on/off · `g`/`h`/`k` lock channel 2/48/79 · `s` sweep all three · `r`
re-init.

---

## 4. If nothing is received

The SPI/calibration milestone is reliable; the *reception* config is a best first guess and
the on-air deviation / IF bandwidth may still need tuning. Don't spin on it — capture the
monitor output (even "nothing") and send it to me. We iterate the ID window, whitening and the
modem registers from there. The payoff if it locks: the A7105 shows the **real** (de-whitened,
CRC-checked) command bytes, which our earlier chips never could — that's what makes a working
transmitter realistic.
