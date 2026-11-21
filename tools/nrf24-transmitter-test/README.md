# nRF24 transmitter test (didn't work)

This was my test of whether an nRF24L01+ could replace the CC2500 as the transmitter. It runs
on the same ESP32-S3 + nRF24 board as the button recorder.

The nRF24 always sends a one-byte preamble and then the address. With the address set to
`CC CC CC CC B9` (preamble becomes `AA`) and the payload `A1 99` + the 17-byte frame, CRC and
auto-ack off, the packet on air is byte for byte the remote's. The timing is the same as the
CC2500 version.

It transmitted fine (about 1310 packets per tap, no failures), but the lamp never reacted. My
best guess is that the nRF24's frequency deviation at 250 kbps (about ±160 kHz) is too narrow.
More in [`docs/hardware.md`](../../docs/hardware.md).

## Build and flash

```
pio run -e esp32s3 -t upload -t monitor
```

The serial monitor runs at 115200 baud.

## Keys

| Key | What it does |
|---|---|
| `1`-`8` | tap that button |
| `h` then `1`-`8` | hold that button |
| `>` / `<` | hold time up/down |
| `]` / `[` | power: -18 / -12 / -6 / 0 dBm (before the module's amplifier) |
| `?` | help |
