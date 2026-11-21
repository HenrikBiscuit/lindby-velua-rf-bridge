# CC2500 raw capture

This is the tool that found the missing preamble. It runs on the same ESP32 + CC2500 hardware
as the transmitter, but puts the CC2500 in receive mode with no sync word at all. It just
demodulates everything in back-to-back 48-byte windows and keeps the ones with a strong signal.
The ESP32 then looks for `B9 A1 99` at any bit offset and prints:

- the 40 bits in front of the sync word (this is where the `AA` preamble showed up)
- which known frame it is, or how many bits are wrong
- the bits after the 17-byte frame
- the time between packets, which gave me the 1245 µs spacing
- a frequency offset estimate (not very reliable, since the window also contains noise)

If your remote uses a different sync word you'll need to change `findSync()`.

Wiring is the same as the transmitter, see [`docs/hardware.md`](../../docs/hardware.md).

## Build and flash

```
pio run -e esp32s3 -t upload -t monitor
```

The serial monitor runs at 921600 baud.

## Keys

| Key | What it does |
|---|---|
| `c` | capture for 4 s (press the remote once after "GO"), then print the analysis |
| `r` | live RSSI bars for 4 s, to check that the radio hears the remote at all |
| `g` / `h` / `k` | listen on 2402 / 2448 / 2479 MHz |
| `]` / `[` | RSSI gate up/down by 5 dB (raise it if the buffer fills with Wi-Fi noise) |
| `m` | switch between free-running and carrier-sense mode |
| `}` / `{` | carrier-sense threshold up/down |
| `x` | hex dump of kept windows that had no sync word |

Carrier-sense mode never triggered for me, which is why free-running is the default.

The capture that found the preamble is in
[`evidence/03_cc2500_raw_capture_real-preamble.log`](../../evidence/03_cc2500_raw_capture_real-preamble.log).
