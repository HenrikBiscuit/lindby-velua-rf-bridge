# A7105 scanner (didn't work out)

This was my attempt at receiving the remote with an AMICCOM A7105 (XL7105-SY-B module) on a
spare ESP32-S3. The idea was that the A7105 has its own hardware preamble, ID sync, CRC and
data whitening, so if the remote used an A7105-style chip, it could receive the packets
natively and show me the real bytes.

It didn't work. SPI and calibration were fine, and an RSSI scan clearly showed the remote at
2402 MHz, but it never decoded a single packet. I tried 4 data rates, 2 IF bandwidths, 4 ID
guesses and 4 preamble lengths. My best guess is that the remote's deviation (around ±250 kHz)
is too wide for the A7105's IF filter. I kept the code here because it's part of the story.

## Module pins

My module has 14 unmarked castellated pads, 7 on each long side. I worked out the pinout with
a multimeter in continuity mode, using the AMICCOM/ElectroDragon reference layout:

| Pad | Signal | Pad | Signal |
|----:|--------|----:|--------|
| 1 | NC | 8 | NC |
| 2 | GIO1 | 9 | VDD (3.0-3.3 V) |
| 3 | NC | 10 | GND |
| 4 | GIO2 | 11 | NC |
| 5 | NC | 12 | SCS |
| 6 | NC | 13 | SCK |
| 7 | GND | 14 | SDIO |

GND beeps against the crystal can, and VDD goes to the plus side of the decoupling caps near
the chip. The module is 3.3 V only.

## Wiring I used

| A7105 pad | Signal | ESP32-S3 |
|---|---|---|
| 9 | VDD | 3V3 |
| 7 or 10 | GND | GND |
| 12 | SCS | GPIO10 |
| 13 | SCK | GPIO12 |
| 14 | SDIO | GPIO11 (3-wire SPI, bit-banged) |
| 2 | GIO1 | GPIO13 |
| 4 | GIO2 | not connected |

## Serial monitor

On startup it prints an SPI round-trip check and the calibration result:

```
A7105 SPI round-trip: wrote B9 A1 99 0D, read B9 A1 99 0D -> OK (wiring good)
  calibration: IF 0x22=0x.. (FBCF ok), VCO 0x25=0x.. (VBCF ok)
```

Received packets would have been printed as `P <ms> ch<2|48|79> crcOK fecok <bytes>`, but I
never got one.

Keys: `i` next ID guess, `w` whitening on/off, `c` CRC on/off, `f` FEC on/off,
`g`/`h`/`k` lock to channel 2/48/79, `s` sweep all three, `r` re-init.

Build with `pio run -e esp32s3 -t upload -t monitor` (115200 baud).
