# Lindby Velua remote clone (ESP32 + CC2500)

My living room ceiling lamp is a Lindby Velua CCT lamp. It came with a small 8-button 2.4 GHz
remote and that's it. No app, no hub, no Zigbee. I wanted to control it from Home Assistant,
so I tried to clone the remote with an ESP32 and a cheap radio module.

It took a lot longer than I expected, but it works now. An ESP32-S3 with a CC2500 module can
send all 8 buttons (both short taps and holds), and the lamp reacts every time, even from the
next room with a couple of doors in between.

```
Home Assistant --(Wi-Fi)--> ESP32-S3 --SPI--> CC2500 + CC2591 PA ))) 2.4 GHz ))) lamp
```

Status: all 8 buttons work. There is also an ESPHome version in
[firmware/esphome](firmware/esphome) that puts the buttons into Home Assistant.

The protocol details are in [docs/protocol.md](docs/protocol.md) and the hardware I used is in
[docs/hardware.md](docs/hardware.md).

## How it went

### Finding the signal

The remote's PCB says YM-2.4G-B. It has one unmarked 16-pin chip, a 16 MHz crystal and a PCB
antenna, so the only thing I really knew was "2.4 GHz".

I started with an nRF24L01+ in the well known promiscuous mode and narrowed it down from
there. The remote uses 2-FSK at 250 kbps and hops between three channels (2402, 2448 and
2479 MHz). Every packet contained the bytes `B9 A1 99 0D BF 29`. Once I set `B9 A1 99` as the
nRF24's receive address I got clean 17-byte frames instead of noise.

### The rolling code that wasn't

Replaying a captured On/off press only worked now and then, and the frame changed on every
press, so I assumed it was a rolling code.

After logging 30 presses it turned out to be a simple 8-state counter that goes round in a
fixed order. Each press sends two bursts, a press frame for about 100 ms and a release frame
for about 1.6 s, so I ended up with 16 frames in total. XOR-ing frames against each other also
showed that the frame is linear over GF(2), so it's some kind of CRC style scramble and not
real crypto.

Then I sent the same counter value three times in a row and the lamp toggled three times. So
the lamp doesn't check the counter at all. It also meant that repeating a command "to be safe"
is a bad idea, because every repeat is another toggle.

### A bit-perfect copy that only worked 30% of the time

For transmitting I used a TI CC2500 module with a CC2591 power amplifier. I checked my own
transmissions with the nRF24 and they were identical to the remote's, bit for bit. Even so,
the lamp only reacted to roughly one press in three.

The real remote works every time through walls, and mine was unreliable even right next to
the lamp. Things I tried that didn't fix it:

- More power. Full power actually made it worse.
- Less power, in case I was overloading the lamp's receiver at short range.
- An AMICCOM A7105 module, hoping it could receive the remote with its own hardware
  preamble/CRC handling. It could see the signal in an RSSI scan but never decoded a packet.
  I think the remote's deviation (around ±250 kHz) is too wide for it.
- A lot of parameter tweaking that didn't lead anywhere.

### A real bug, but not the one

Going through the CC2500 config register by register I found `MCSM1 = 0x32`. My comment said
it would go to FSTXON after each packet, but `10` in those bits means "stay in TX". So the
radio kept transmitting between packets, every packet waited out a 3 ms timeout, and the PA was
on almost all the time. That also explains why full power made things worse.

After fixing it (`0x31`) the transmitter behaved perfectly: 849 µs per packet, no timeouts, no
FIFO underflows. The lamp still only reacted about 30% of the time.

### Using the CC2500 to look at the raw signal

At this point I wanted an SDR, but an RTL-SDR can't go up to 2.4 GHz and I didn't have a
HackRF. So I thought about what my receivers weren't showing me. Both the nRF24 and the CC2500
wait for a sync word and only give you the bytes after it. I had never seen anything the remote
sends before `B9 A1 99`.

So I set the CC2500 to receive with no sync word at all. It just demodulates everything into
48-byte windows, and the ESP32 looks for `B9 A1 99` at any bit offset. In 4 seconds it caught
189 packets from the remote, and the bits before the sync word were the same every time:

```
remote:  10101010 11001100 11001100 11001100 11001100 | B9 A1 99 | frame
mine:     1001100 11001100 11001100 11001100 11001100 | B9 A1 99 | frame
```

The remote starts every packet with `10101010` (0xAA), which is a preamble. The receiver uses
it to adjust its gain and lock onto the bit timing before it starts looking for the sync word.
My packets never had it, so the lamp only caught them when it got lucky.

The same capture gave me the real timing as well: one packet every 1245 µs, three per channel,
and about 11.7 ms for a full round of all three channels.

I changed the packet to `AA AA AA AA CC CC CC CC B9 A1 99` plus the frame, sent it on the same
timing as the remote, and calibrated each channel once at startup so hopping is instant. After
that every press worked. Switching back to the old packet brought the misses straight back, so
the missing preamble was the real problem.

### The other 7 buttons

Once that worked the rest was quick. I turned the nRF24 board into a recorder that groups
everything the remote sends into events, ignores packets that only show up once (garbled
ones) and prints the order the frames came in. I did three taps and one long hold for each
button, which took a few minutes.

What came out of it:

- The release frame is the same for all buttons.
- Each button's press frame is the On/off press frame XOR a fixed value, and that value is the
  same for every counter state. So a few captures per button were enough to generate all 8
  states for all 8 buttons.
- Holding a button just repeats the press frame until you let go, and then the release is sent.
  The lamp keeps dimming or changing colour temperature as long as the press frame keeps coming.

## What I learned

- Packet-mode receivers hide the preamble and sync. If you're trying to clone a signal you need
  some way of seeing the bits in front of the sync word.
- Matching bytes after the sync word doesn't mean the whole transmission matches. Preamble,
  timing and what happens between packets matter too.
- One simple test (sending the same code three times) killed the rolling code theory. I should
  have done it much earlier.
- When a few rounds of tuning don't help, it's time to build a way to measure instead.
- The CC2500 in free-running receive mode did the job I thought I needed an SDR for.
- Check the register map instead of trusting your own comments. `0x32` vs `0x31` had been wrong
  in every version.

## What's in the repo

```
firmware/velua-transmitter/     ESP32 + CC2500 transmitter, all buttons, tap and hold
firmware/esphome/               the same transmitter as an ESPHome device for Home Assistant
tools/cc2500-raw-capture/       CC2500 raw capture without sync word (found the preamble)
tools/nrf24-button-recorder/    nRF24 recorder I used to decode the buttons
tools/a7105-scanner/            A7105 bring-up and scanner (didn't work out)
archive/                        older transmitter and receiver versions (v3 to v9)
evidence/                       serial logs from the important steps
docs/protocol.md                protocol notes, frames and button table
docs/hardware.md                the hardware I used and how it's wired
```

## Running it

1. Wire the ESP32-S3 to the CC2500 module as described in [docs/hardware.md](docs/hardware.md).
2. Build and flash with PlatformIO:
   ```
   cd firmware/velua-transmitter
   pio run -e esp32s3 -t upload -t monitor
   ```
3. In the serial monitor, `1`-`8` taps a button and `h` followed by `1`-`8` holds it. `>` and
   `<` change the hold time, `]` and `[` change the TX power.

Another remote will most likely have a different ID (the `0D BF 29` bytes and the check bytes),
so you would have to record your own with `tools/nrf24-button-recorder` and replace the tables.

## Equipment I used

- 3x ESP32-S3-DevKitC-1
- VT-CC2500PA-S2 module (CC2500 + CC2591)
- nRF24L01+PA+LNA module
- XL7105-SY-B module (A7105)
- VS Code with PlatformIO

No SDR or logic analyser, and I didn't solder anything on the remote since it's the only one I
have. More detail in [docs/hardware.md](docs/hardware.md).

## Disclaimer

This is my own lamp and my own remote. The lamp doesn't authenticate anything, so please keep
the transmit power low and only send when you actually need to.

I used Claude (Anthropic) for help with debugging and analysis along the way.
