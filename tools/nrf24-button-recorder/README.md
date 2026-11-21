# nRF24 button recorder

I used this to decode all 8 buttons in a few minutes. It runs on an ESP32-S3 with an
nRF24L01+ module that listens for `B9 A1 99` as its address at 250 kbps.

It groups everything the remote sends into events (an event ends after 300 ms of silence) and
for each event prints:

- every distinct 17-byte frame that was seen at least twice, in the order it first appeared,
  with a count and first/last time (packets seen only once are garbled and get dropped)
- the time sequence of those frames, so you can see press, hold and release

Wiring is in [`docs/hardware.md`](../../docs/hardware.md) under "Receiver".

## Build and flash

```
pio run -e esp32s3 -t upload -t monitor
```

The serial monitor runs at 921600 baud.

## Keys

| Key | What it does |
|---|---|
| `1`-`8` | label the button you're about to record (goes into the log) |
| `g` / `h` / `k` | listen on 2402 / 2448 / 2479 MHz (default 2402) |
| `v` | also print every raw packet |

## How I recorded the buttons

For each button: type its number, tap it 3 times with about 3 s in between, then hold it for
about 3 s. That's 4 events per button. The log from my remote is in
[`evidence/04_nrf24_all-8-buttons.log`](../../evidence/04_nrf24_all-8-buttons.log).
