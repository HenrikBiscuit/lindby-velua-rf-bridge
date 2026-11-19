# Hardware

## What I used

| Part | Role | Notes |
|---|---|---|
| 3× ESP32-S3-DevKitC-1 (N16R8) | one per radio | any ESP32 works; avoid GPIO 35–37 (PSRAM), 0/3/45/46 (strapping), 19/20 (USB) |
| **VT-CC2500PA-S2** (TI CC2500 + CC2591 PA/LNA, 26 MHz) | **the transmitter** — and, in receive mode, a "poor man's SDR" | 3.3 V only. PA/LNA pins are **swapped vs. the module guide**: TX = PA 1 / LNA 0, RX = LNA 1 / PA 0 |
| nRF24L01+PA+LNA (clone) | receiver / frame recorder | locks onto `B9 A1 99` as a 3-byte address at 250 kbps. Clone quirk: keep RF_SETUP bit 0 set |
| XL7105-SY-B (AMICCOM A7105) | dead end | found the signal in an RSSI scan but could never demodulate it (deviation too wide for its IF) |

### Transmitter wiring (ESP32-S3 ↔ VT-CC2500PA-S2)

| CC2500PA pad | ESP32-S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SI | GPIO11 |
| SCLK | GPIO12 |
| SO | GPIO13 |
| CSN | GPIO10 |
| GDO0 | GPIO4 |
| GDO2 | GPIO5 |
| PA | GPIO6 |
| LNA | GPIO7 |

Never drive PA and LNA high at the same time. Power: `PATABLE 0x60` is plenty for
"a couple of doors away"; the full `0xFE` is unnecessary.

### Receiver wiring (ESP32-S3 ↔ nRF24L01+)

| nRF24 | ESP32-S3 |
|---|---|
| VCC | 3V3 (**never 5 V**) |
| GND | GND |
| CE | GPIO9 |
| CSN | GPIO10 |
| SCK | GPIO12 |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| IRQ | GPIO14 |

## Minimal setup (for just controlling the lamp)

You only need **one ESP32 and one CC2500-class radio**. The receiver boards were for
investigation; the frames are now in [`protocol.md`](protocol.md).

| Option | Approx. cost | Status |
|---|---|---|
| ESP32 + **VT-CC2500PA-S2** (CC2500 + PA) | €8–15 | ✅ **what I run** — reliable through two doors |
| ESP32 + **plain CC2500 module** (no PA/LNA, ~0 dBm) | €5–8 | 🟡 same chip and code, minus the PA pin handling; the original remote is a tiny PCB antenna, so range is probably fine — **not yet tested** |
| ESP32 + **nRF24L01+** (no PA) | €3–5 | 🟡 **idea, not tested.** The nRF24 always sends a one-byte preamble (`AA` when the address starts with a 1-bit) and up to a 5-byte address. With address **`CC CC CC CC B9`** and payload **`A1 99` + 17-byte frame**, CRC off, 250 kbps, its packet is byte-for-byte the remote's packet. Open question: its FSK deviation is narrower than the remote's |
| ESP32-C3 "SuperMini" instead of an S3 DevKit | saves a few € | any ESP32 with SPI and Wi-Fi works |

Not suitable: **RTL-SDR** dongles stop around 1.7 GHz and cannot hear 2.4 GHz. A HackRF-class
SDR would work but turned out to be unnecessary — see the README for how the CC2500 itself
was used to capture the raw signal.

## Regulatory note

This is a short-range device on the 2.4 GHz ISM band: stay within your local limits (in the
EU about 10 mW EIRP for this kind of device). Use the lowest power that works, transmit only
when a button is "pressed", and never transmit without an antenna attached.
