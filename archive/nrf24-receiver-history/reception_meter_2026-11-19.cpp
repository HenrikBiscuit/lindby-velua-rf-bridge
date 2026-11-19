// RECEIVE BOARD (ESP32 + nRF24L01+PA+LNA) - 250 kbps checker, receive only   2026-11-19
// Project: velua-nrf24 -> src/main.cpp   (monitor 921600, wiring unchanged)
//
// The remote sends 250 kbps and every packet contains the constant bytes
// B9 A1 99 0D BF 29. The nRF24 can receive 250 kbps natively, so we let it match on
// "B9 A1 99" as its 3-byte address: whatever follows (should start 0D BF 29) lands in the
// payload, bit-aligned. That gives an independent check: does OUR transmission come out
// exactly like the REAL remote's?
//
// The nRF24 sends/compares the address bytes in an order that depends on how it is
// written, so pipe 0 and pipe 1 get the two possible byte orders. One of them should hit.
//
// Output:  P <ms> p<pipe> <32 bytes hex> [*]   (* = starts with 0D BF 29, i.e. a clean packet)
//          S <ms> pk=<packets in 1 s> good=<packets starting 0D BF 29>
// Keys: 0-9 = marker   h/k/g = channel 48/79/2 (2448/2479/2402 MHz)
//       p = toggle promiscuous mode (2-byte 0x0055 trick at 250 kbps) as a fallback
#include <Arduino.h>
#include <SPI.h>

#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CSN   10
#define PIN_CE     9
#define PIN_IRQ   14

SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);
static uint8_t channel = 48;
static bool promisc = false;

static uint8_t readReg(uint8_t r) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(r & 0x1F); uint8_t v = SPI.transfer(0xFF);
  digitalWrite(PIN_CSN, HIGH); SPI.endTransaction(); return v;
}
static void writeReg(uint8_t r, uint8_t v) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(0x20 | (r & 0x1F)); SPI.transfer(v);
  digitalWrite(PIN_CSN, HIGH); SPI.endTransaction();
}
static void writeBuf(uint8_t r, const uint8_t *b, int n) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(0x20 | (r & 0x1F)); for (int i = 0; i < n; i++) SPI.transfer(b[i]);
  digitalWrite(PIN_CSN, HIGH); SPI.endTransaction();
}
static void command(uint8_t c) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(c); digitalWrite(PIN_CSN, HIGH); SPI.endTransaction();
}
static void readPayload(uint8_t *b) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(0x61); for (int i = 0; i < 32; i++) b[i] = SPI.transfer(0xFF);
  digitalWrite(PIN_CSN, HIGH); SPI.endTransaction();
}

static void configure() {
  digitalWrite(PIN_CE, LOW);
  writeReg(0x00, 0x00);                  // power down while configuring
  writeReg(0x01, 0x00);                  // no auto-ack
  writeReg(0x04, 0x00);                  // no retries
  writeReg(0x06, 0x27);                  // 250 kbps (RF_DR_LOW), bit 0 kept set (clone chip)
  writeReg(0x05, channel);
  writeReg(0x1C, 0x00); writeReg(0x1D, 0x00);
  if (!promisc) {
    writeReg(0x03, 0x01);                // 3-byte address
    const uint8_t a0[3] = {0x99, 0xA1, 0xB9};   // register order LSB first -> on air B9 A1 99 (expected)
    const uint8_t a1[3] = {0xB9, 0xA1, 0x99};   // the other order, just in case
    writeBuf(0x0A, a0, 3);
    writeBuf(0x0B, a1, 3);
    writeReg(0x02, 0x03);                // pipes 0 and 1
    writeReg(0x11, 32); writeReg(0x12, 32);
  } else {
    writeReg(0x03, 0x00);                // illegal 2-byte address
    const uint8_t a[2] = {0x55, 0x00};
    writeBuf(0x0A, a, 2);
    writeReg(0x02, 0x01);
    writeReg(0x11, 32);
  }
  writeReg(0x00, 0x03);                  // PWR_UP + PRIM_RX, CRC off
  delay(5);
  command(0xE2);                         // FLUSH_RX
  writeReg(0x07, 0x70);
  digitalWrite(PIN_CE, HIGH);
  Serial.printf("\n>> nRF24 ch %u (%u MHz), 250 kbps, %s\n", channel, 2400 + channel,
                promisc ? "promiscuous 0x0055" : "address B9 A1 99 (pipe0) / 99 A1 B9 (pipe1)");
}

void setup() {
  pinMode(PIN_CE, OUTPUT); digitalWrite(PIN_CE, LOW);
  pinMode(PIN_IRQ, INPUT_PULLUP);
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  Serial.begin(921600);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  delay(150);
  configure();
  Serial.printf("Receive board 250k checker. CONFIG 0x%02X SETUP_AW 0x%02X RF_SETUP 0x%02X (expect 0x03 / 0x01 / 0x27 or 0x26)\n",
                readReg(0x00), readReg(0x03), readReg(0x06));
  Serial.println("RECEPTION METER - place next to the lamp. One summary per tap (STRONG/OK/WEAK).");
  Serial.println("Keys: g/h/k = channel 2/48/79 | v = per-frame printing");
}

// RECEPTION METER: place this board next to the LAMP. Each tap from the transmit board shows
// up as a burst of frames; we print one summary per tap with how many clean frames landed and
// a STRONG/OK/WEAK verdict. Tune the transmit board's power/aim to push every tap to STRONG.
//   keys: g/h/k = channel 2/48/79 (2402/2448/2479 MHz)   v = toggle per-frame printing
static bool verbose = false;

void loop() {
  static uint32_t tapPk = 0, tapGood = 0, lastPkt = 0, tapNo = 0;
  static uint8_t  lastCnt0 = 0, lastCnt1 = 0;
  static bool     inTap = false;

  while (Serial.available()) {
    char c = Serial.read();
    if      (c == 'h') { channel = 48; configure(); }
    else if (c == 'k') { channel = 79; configure(); }
    else if (c == 'g') { channel = 2;  configure(); }
    else if (c == 'v') { verbose = !verbose; Serial.printf("verbose %s\n", verbose ? "on" : "off"); }
  }

  uint8_t st = readReg(0x07);
  if (digitalRead(PIN_IRQ) == LOW || (st & 0x40)) {
    uint8_t b[32];
    readPayload(b);
    writeReg(0x07, 0x40);
    bool clean = (b[0] == 0x0D && b[1] == 0xBF && b[2] == 0x29);
    tapPk++;
    if (clean) { tapGood++; lastCnt0 = b[3]; lastCnt1 = b[4]; }
    inTap = true;
    lastPkt = millis();
    if (verbose) {
      Serial.printf("  P %lu ", millis());
      for (int i = 0; i < 32; i++) Serial.printf("%02X", b[i]);
      Serial.println(clean ? " *" : "");
    }
  }

  // a tap has ended when the burst goes quiet for >250 ms
  if (inTap && millis() - lastPkt > 250) {
    const char *verdict = tapGood >= 30 ? "STRONG" : tapGood >= 10 ? "OK" : "WEAK";
    Serial.printf("TAP #%lu  ch%u  cnt=%02X%02X  clean=%lu / %lu frames  -> %s\n",
                  ++tapNo, channel, lastCnt0, lastCnt1, tapGood, tapPk, verdict);
    tapPk = tapGood = 0; inTap = false;
  }
}