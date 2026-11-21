# A7105 transmitter test (didn't work)

My second attempt at a cheaper transmitter, using the A7105 module (XL7105-SY-B) from the
[A7105 scanner](../a7105-scanner). The wiring is the same as there.

The A7105 sends a 4-byte preamble, a 4-byte ID code (`CC CC CC CC`) and then the payload
(`B9 A1 99` + the 17-byte frame), with CRC, FEC and whitening off. On paper that matches the
remote's packet. The timing is the same as the CC2500 version.

Since I didn't know which register settings give the right frequency deviation, the sketch has
an automatic sweep. It sends one On/off tap every 4 s over 52 combinations: normal, inverted and
LSB-first bits, each with 13 different deviation settings. I stood by the lamp and watched. None
of them did anything. The register layout was partly a guess because I couldn't get a readable
datasheet. More in [`docs/hardware.md`](../../docs/hardware.md).

## Build and flash

```
pio run -e esp32s3 -t upload -t monitor
```

The serial monitor runs at 115200 baud.

## Keys

| Key | What it does |
|---|---|
| `s` | start the sweep (20 s countdown, then 52 taps 4 s apart, any key stops it) |
| `tNN` | run sweep trial NN again, e.g. `t07` |
| `1`-`8` | tap that button with the current settings |
| `h` then `1`-`8` | hold that button |
| `>` / `<` | hold time up/down |
| `(` / `)` | register `0x15` down/up by one |
| `rAAVV` | write register `0xAA` = `0xVV` (hex), e.g. `r1530` |
| `d` | dump registers `0x00`-`0x32` |
| `?` | help |

Each trial is printed with a timestamp in the log, so you only need to note the time when the
lamp reacts and match it up afterwards.
