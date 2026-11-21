# Transmitter (serial monitor version)

This is the plain PlatformIO version of the transmitter: an ESP32-S3 with a CC2500 module that
sends any of the 8 remote buttons, as a tap or a hold. I used it for testing before I moved to
the ESPHome version in [`../esphome`](../esphome), which does the same thing from Home Assistant.

Wiring is in [`docs/hardware.md`](../../docs/hardware.md).

## Build and flash

```
pio run -e esp32s3 -t upload -t monitor
```

The serial monitor runs at 115200 baud.

## Keys

| Key | What it does |
|---|---|
| `1`-`8` | tap that button |
| `h` then `1`-`8` | hold that button (2 s by default) |
| `>` / `<` | hold time up/down in 0.5 s steps |
| `]` / `[` | TX power up/down (PATABLE level, saved across reboots) |
| `?` | help |

The BOOT button on the board also sends an On/off tap.

Button numbers: 1 On/off, 2 Night, 3 Brightness up, 4 Brightness down, 5 Cycle colour
temperature, 6 Timer 60 s, 7 Cooler, 8 Warmer.

After each press it prints how many packets were sent and the average spacing, which should be
around 1298 µs (1245 µs per packet plus a short gap at every channel hop).
