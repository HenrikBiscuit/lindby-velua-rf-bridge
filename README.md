# Lindby Velua → ESP32: reverse-engineering a 2.4 GHz lamp remote

My living-room ceiling lamp — a **Lindby Velua** CCT LED lamp — came with an 8-button 2.4 GHz
remote and **nothing else**: no app, no hub, no Zigbee, no documentation of its radio protocol.
I wanted it in Home Assistant, so I set out to clone the remote with an ESP32 and a €10 radio module.

It took three radios, a lot of wrong theories and one very sneaky missing byte, but it now works:
**an ESP32 + CC2500 sends all 8 buttons — tap and hold — and the lamp reacts every time, from
two rooms away, exactly like the original remote.**

```
 Home Assistant ──(Wi-Fi)──► ESP32-S3 ──SPI──► CC2500 + CC2591 PA ))) 2.4 GHz ))) Lindby Velua
```

| | |
|---|---|
| **Result** | All 8 buttons, tap + hold, 100 % reliable in testing |
| **Protocol** | 2-FSK, 250 kbps, 3-channel hopping, 17-byte frames — fully documented in [`docs/protocol.md`](docs/protocol.md) |
| **Hardware** | ESP32 + CC2500 module — see [`docs/hardware.md`](docs/hardware.md), including cheaper options |
| **Next** | Home Assistant integration (ESPHome) |

---

## The story

### 1. Finding the signal

The remote (PCB marking *YM-2.4G-B*) has one unmarked 16-pin chip, a 16 MHz crystal and a
PCB antenna. No FCC ID, no part number. All I knew was "2.4 GHz".

I started with the cheapest 2.4 GHz radio there is, an **nRF24L01+**, in its well-known
"promiscuous" trick mode, and slowly narrowed things down: the remote talks **2-FSK at
250 kbps**, hops over **three channels** (2402 / 2448 / 2479 MHz), and every packet contains
the same bytes `B9 A1 99 0D BF 29`. Using `B9 A1 99` as the nRF24's receive address turned the
chaos into clean, bit-aligned 17-byte frames.

### 2. The "rolling code" that wasn't

Replaying a captured On/off press didn't work reliably, and the frames changed on every press.
That looked a lot like a **rolling code** — the kind that makes replay attacks impossible.

Logging 30 presses showed the "rolling code" is just an **8-state counter** that cycles in a
fixed order. Every press actually sends two bursts — a *press* frame (~100 ms) and a *release*
frame (~1.6 s) — and I captured all 16. Even better: the frame turned out to be **linear over
GF(2)** (a CRC-style scramble, not cryptography), which I proved by XOR-ing frames together.

Then the decisive test: sending **the same** counter state three times made the lamp flicker
**three times**. The lamp doesn't check the counter at all. The rolling code was a red herring,
and a useful lesson: *one tap = one toggle*, so "repeating for reliability" just turns the
result into an odd/even lottery.

### 3. A perfect copy that only works 30 % of the time

Now I could transmit, with a **TI CC2500** module (with a CC2591 power amplifier), frames that
were **bit-for-bit identical** to the remote's — verified by receiving my own transmission on
the nRF24. And the lamp… sometimes toggled. Maybe one press in three.

The real remote works first time, every time, through walls. My bit-perfect copy was flaky even
next to the lamp, and oddly sensitive to power and antenna direction. I tried:

- **More power** — full power made it *worse*.
- **Less power** (a strong signal can overload a receiver at close range) — no real change.
- **A third radio**, an **AMICCOM A7105** module, hoping to receive the remote "natively" with
  its hardware preamble/CRC/whitening. It found the signal in an RSSI scan but could never
  demodulate it (the remote's ±250 kHz deviation is too wide for it). Dead end.
- **Hours of parameter sweeps.** None of them were decisive.

### 4. Bug #1: the transmitter that never stopped transmitting

Re-reading my CC2500 configuration line by line turned up a real bug: `MCSM1 = 0x32`. The comment
said "go to FSTXON after a packet", but those bits mean **"stay in TX"**. The radio kept the
carrier on between packets, so each packet ran into a 3 ms timeout, and the PA ran close to
100 % duty. That also explained why full power "broke" things.

After the fix the transmitter was textbook-clean: 849 µs per packet, zero timeouts, zero
underflows. The lamp: **still about 30 %.** A real bug — but not *the* bug.

### 5. The breakthrough: using the transmitter as an SDR

The obvious next step was a **software-defined radio** to see the raw signal — but an RTL-SDR
can't reach 2.4 GHz, and a HackRF is expensive. Instead I asked: what are my receivers *hiding*?

Every digital receiver I used — nRF24 and CC2500 alike — searches for a sync word and hands me
**only the bytes after it**. Whatever the remote sends *before* `B9 A1 99` had never been seen.

So I reprogrammed the CC2500 to receive with **no sync word at all**: just demodulate everything
in back-to-back 48-byte windows, then search for `B9 A1 99` at any bit offset in software. In
four seconds it caught 189 remote packets, and the 40 bits in front of the sync word lined up
perfectly:

```
real remote:  10101010 11001100 11001100 11001100 11001100 | B9 A1 99 | frame
my packet:     1001100 11001100 11001100 11001100 11001100 | B9 A1 99 | frame
              ^^^^^^^^
```

The remote starts every packet with **`10101010` — a preamble** (`0xAA`). That alternating
pattern is what a receiver uses to set its gain and lock onto the bit clock *before* it looks
for the sync word. My packets didn't have one. The lamp's receiver only locked on by luck: about
30 % of the time.

The same capture also gave the remote's real timing (one packet every 1245 µs, 3 per channel,
11.7 ms per hop cycle) and showed that nothing follows the frame.

**Transmitter v9:** `AA AA AA AA CC CC CC CC B9 A1 99` + frame, sent on the remote's exact
time grid, with each channel calibrated once at boot for instant hopping.
**Result: every tap works.** An A/B switch back to the old packet brought the failures straight
back — so the missing preamble was the root cause.

### 6. All 8 buttons in three minutes

With a reliable link, the rest went fast. I turned the nRF24 into a **button recorder**: it
groups everything the remote sends into events, keeps only frames seen at least twice (garbled
packets appear once) and prints the time sequence. Three taps and one long hold per button —
about three minutes for the whole remote.

The structure fell right out of the data:

- The **release** frame is **identical for every button** ("button up").
- Each button's **press** frame = the On/off press frame **XOR a constant** — the same
  constant for every counter state. So 4 captures per button were enough to generate all
  8 counter states for all 8 buttons.
- A **hold** is simply the press frame repeated for as long as the button is held; the lamp
  ramps brightness or colour temperature until the release arrives.

---

## Lessons learned

1. **Your receiver lies by omission.** Packet-mode radios strip the preamble and sync. If you
   want to clone a signal, find a way to see the bits *before* the sync word.
2. **"Bit-identical" isn't "identical".** Matching bytes after the sync word says nothing about
   the preamble, timing, or what the transmitter does between packets.
3. **Test the theory, not the symptom.** The "rolling code" died with a single experiment: send the
   same code three times.
4. **Stop sweeping, start measuring.** Two or three rounds of tuning without progress is the
   signal to build a measurement tool instead.
5. **A transceiver is a lab instrument.** The CC2500 in "no sync, free-running" mode did the job
   I thought needed an SDR.
6. **Read the register map, not the comment.** `0x32` vs `0x31` kept a carrier on through every earlier version.

## Repository layout

```
firmware/velua-transmitter/     ESP32 + CC2500: sends any button, tap or hold (the result)
tools/cc2500-raw-capture/       CC2500 as a no-sync raw capture tool (found the preamble)
tools/nrf24-button-recorder/    nRF24 event recorder used to decode all 8 buttons
tools/a7105-scanner/            A7105 bring-up and scanner (the dead end, kept for the story)
archive/                        earlier transmitter/receiver versions, v3 → v9
evidence/                       the serial logs behind each milestone
docs/protocol.md                the full protocol: RF, packet, frames, button table
docs/hardware.md                parts, wiring, minimal/cheaper setups
```

## Quick start

1. Wire an ESP32-S3 to a CC2500 module as in [`docs/hardware.md`](docs/hardware.md).
2. Build and flash with PlatformIO:
   ```
   cd firmware/velua-transmitter
   pio run -e esp32s3 -t upload -t monitor
   ```
3. In the serial monitor: `1`–`8` taps a button, `h` then `1`–`8` holds it (`>` / `<` change the
   hold time), `]` / `[` change the TX power.

Your remote probably has a different ID (bytes `0D BF 29` and the check bytes). Record it with
`tools/nrf24-button-recorder` (or the raw capture tool) and update the tables in the transmitter.

## Equipment

3× ESP32-S3-DevKitC-1, VT-CC2500PA-S2 (CC2500 + CC2591), nRF24L01+PA+LNA, XL7105-SY-B (A7105),
VS Code + PlatformIO. No SDR, no logic analyser, no soldering on the original remote.
Details in [`docs/hardware.md`](docs/hardware.md).

## Disclaimer

This controls my own lamp with my own remote's codes. The lamp does not authenticate anything,
so be considerate: transmit only when needed, at low power, within your local radio rules.

Built with Claude (Anthropic) as a debugging and analysis partner.
