// nRF24L01+ AS TRANSMITTER - test if a cheap nRF24 can replace the CC2500   2026-11-19
// Board: the nRF24 receive board (same wiring as velua-nrf24). Monitor 115200.
//
// Idea: the nRF24 always sends [1-byte preamble][address][payload]. The preamble is 0xAA when
// the first address bit is 1. With the 5-byte address CC CC CC CC B9 and the payload
// A1 99 + 17-byte frame (no CRC, no auto-ack, static payload) the packet on air is
//     AA | CC CC CC CC B9 | A1 99 + frame
// which is byte for byte the remote's packet. Same timing as the CC2500 version:
// 1245 us slots, 3 packets per channel, channels 2 / 48 / 79.
//
// Keys: 1-8 tap button | h then 1-8 hold button | >/< hold time | ]/[ power (-18/-12/-6/0 dBm)
#include <Arduino.h>
#include <SPI.h>

#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CSN   10
#define PIN_CE     9
#define PIN_IRQ   14

// ---- protocol (docs/protocol.md) ----
static const int NSTATES = 8, NBTN = 8;
static const uint8_t REL[NSTATES][17] = {
  {0x0D,0xBF,0x29,0xB9,0x22,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xED,0x99,0x1E,0xA7,0x3E,0xAC},
  {0x0D,0xBF,0x29,0xA8,0x32,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xFC,0x89,0xA0,0x18,0xB2,0xF1},
  {0x0D,0xBF,0x29,0xA9,0x11,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xFD,0xAA,0xA1,0xDA,0x63,0xB4},
  {0x0D,0xBF,0x29,0xB8,0x01,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xEC,0xBA,0x1F,0x65,0xEF,0xE9},
  {0x0D,0xBF,0x29,0x8A,0x02,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xDE,0xB9,0xC3,0x26,0x40,0x46},
  {0x0D,0xBF,0x29,0x9B,0x12,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xCF,0xA9,0x7D,0x99,0xCC,0x1B},
  {0x0D,0xBF,0x29,0x9A,0x31,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xCE,0x8A,0x7C,0x5B,0x1D,0x5E},
  {0x0D,0xBF,0x29,0x8B,0x21,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xDF,0x9A,0xC2,0xE4,0x91,0x03},
};
static const uint8_t PRS[NSTATES][17] = {
  {0x0D,0xBF,0x29,0x8B,0x30,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xDF,0x9A,0x4D,0xDD,0x6B,0x2D},
  {0x0D,0xBF,0x29,0xB9,0x33,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xED,0x99,0x91,0x9E,0xC4,0x82},
  {0x0D,0xBF,0x29,0xA8,0x23,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xFC,0x89,0x2F,0x21,0x48,0xDF},
  {0x0D,0xBF,0x29,0xA9,0x00,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xFD,0xAA,0x2E,0xE3,0x99,0x9A},
  {0x0D,0xBF,0x29,0xB8,0x10,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xEC,0xBA,0x90,0x5C,0x15,0xC7},
  {0x0D,0xBF,0x29,0x8A,0x13,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xDE,0xB9,0x4C,0x1F,0xBA,0x68},
  {0x0D,0xBF,0x29,0x9B,0x03,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xCF,0xA9,0xF2,0xA0,0x36,0x35},
  {0x0D,0xBF,0x29,0x9A,0x20,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xCE,0x8A,0xF3,0x62,0xE7,0x70},
};
static const uint8_t BTN_XOR[NBTN][17] = {
  {0,0,0,0,0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0,0,0,0,0,0x10,0x22,0x80,0x88,0x00,0x00,0x10,0x22,0x49,0x97,0xEA,0x0C},
  {0,0,0,0,0,0x20,0x77,0x80,0x88,0x00,0x00,0x20,0x77,0x94,0x16,0x19,0x89},
  {0,0,0,0,0,0x33,0x31,0x80,0x88,0x00,0x00,0x33,0x31,0xB7,0x14,0xDA,0xA9},
  {0,0,0,0,0,0x40,0xDD,0x80,0x88,0x00,0x00,0x40,0xDD,0xC0,0x44,0xAF,0x4E},
  {0,0,0,0,0,0x00,0x11,0x80,0x99,0x80,0x88,0x00,0x00,0x40,0xDD,0x5C,0x3B},
  {0,0,0,0,0,0x00,0x11,0x90,0xBB,0x00,0x00,0x10,0x22,0x68,0xD2,0x24,0x3B},
  {0,0,0,0,0,0x00,0x11,0x91,0x98,0x88,0x00,0x11,0x01,0x5D,0xE8,0x8B,0x94},
};
static const char *BTN_NAME[NBTN] = {"On/off", "Night", "Brightness up", "Brightness down",
                                     "Cycle colour temp", "Timer 60 s", "Cooler", "Warmer"};
static const uint8_t CH[3] = {2, 48, 79};
static const uint32_t SLOT_US = 1245, HOP_EXTRA_US = 160, TAP_MS = 100, RELEASE_MS = 1600;
static const int PAYLOAD_LEN = 19;   // A1 99 + 17-byte frame

// ---- nRF24 low level ----
SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);
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
static void writeBuf(uint8_t cmd, const uint8_t *b, int n) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(cmd); for (int i = 0; i < n; i++) SPI.transfer(b[i]);
  digitalWrite(PIN_CSN, HIGH); SPI.endTransaction();
}
static void command(uint8_t c) {
  SPI.beginTransaction(spiCfg); digitalWrite(PIN_CSN, LOW);
  SPI.transfer(c); digitalWrite(PIN_CSN, HIGH); SPI.endTransaction();
}

static int pwr = 3;   // RF_SETUP power bits: 0=-18, 1=-12, 2=-6, 3=0 dBm (before the module's PA)
static const int PWR_DBM[4] = {-18, -12, -6, 0};

static void configure() {
  digitalWrite(PIN_CE, LOW);
  writeReg(0x00, 0x00);                       // power down, CRC off
  writeReg(0x01, 0x00);                       // EN_AA off -> plain ShockBurst, no PCF
  writeReg(0x02, 0x01);
  writeReg(0x03, 0x03);                       // 5-byte address
  writeReg(0x04, 0x00);                       // no retransmit
  writeReg(0x06, 0x21 | (pwr << 1));          // 250 kbps, power, bit 0 kept set (clone)
  writeReg(0x1C, 0x00); writeReg(0x1D, 0x00); // no dynamic payload
  const uint8_t addr[5] = {0xB9, 0xCC, 0xCC, 0xCC, 0xCC};   // LSB first -> on air CC CC CC CC B9
  writeBuf(0x20 | 0x10, addr, 5);             // TX_ADDR
  writeReg(0x00, 0x02);                       // PWR_UP, PTX, CRC off
  delay(5);
  command(0xE1);                              // FLUSH_TX
  writeReg(0x07, 0x70);
}

static uint32_t stPkts, stLate, stFail, stFirst, stLast;

static void sendOne(const uint8_t *payload, uint32_t at) {
  writeBuf(0xA0, payload, PAYLOAD_LEN);       // W_TX_PAYLOAD
  if ((int32_t)(micros() - at) > 50) stLate++;
  while ((int32_t)(micros() - at) < 0) {}
  uint32_t t = micros();
  digitalWrite(PIN_CE, HIGH); delayMicroseconds(15); digitalWrite(PIN_CE, LOW);
  if (stPkts == 0) stFirst = t;
  stLast = t;
  while (!(readReg(0x07) & 0x20) && micros() - t < 3000) {}   // TX_DS
  if (!(readReg(0x07) & 0x20)) { stFail++; command(0xE1); }
  writeReg(0x07, 0x70);
  stPkts++;
}

static void sendFor(const uint8_t *payload, uint32_t ms, int &ch, int &slot, uint32_t &next) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (slot == 0) writeReg(0x05, CH[ch]);    // hop (we are in standby between packets)
    sendOne(payload, next);
    next += SLOT_US;
    if (++slot == 3) { slot = 0; ch = (ch + 1) % 3; next += HOP_EXTRA_US; }
  }
}

static uint8_t counter = 0;
static uint32_t holdMs = 2000;
static bool holdNext = false;

static void sendButton(int btn, uint32_t pressMs) {
  uint8_t pp[PAYLOAD_LEN] = {0xA1, 0x99}, rp[PAYLOAD_LEN] = {0xA1, 0x99};
  for (int i = 0; i < 17; i++) { pp[2 + i] = PRS[counter][i] ^ BTN_XOR[btn][i]; rp[2 + i] = REL[counter][i]; }
  Serial.printf("%s button %d = %s (%lu ms), state %d, power %d dBm\n", pressMs > TAP_MS ? "HOLD" : "TAP ",
                btn + 1, BTN_NAME[btn], pressMs, counter, PWR_DBM[pwr]);
  stPkts = stLate = stFail = 0;
  int ch = 0, slot = 0;
  uint32_t next = micros() + 300;
  sendFor(pp, pressMs, ch, slot, next);
  sendFor(rp, RELEASE_MS, ch, slot, next);
  Serial.printf("  sent %lu pkts, avg period %lu us, late %lu, failed %lu\n", stPkts,
                stPkts > 1 ? (stLast - stFirst) / (stPkts - 1) : 0, stLate, stFail);
  counter = (counter + 1) % NSTATES;
}

static void help() {
  Serial.printf("nRF24 TX test: 1-8 tap | h+1-8 hold %lu ms | >/< hold time | ]/[ power (%d dBm)\n",
                holdMs, PWR_DBM[pwr]);
}

void setup() {
  pinMode(PIN_CE, OUTPUT); digitalWrite(PIN_CE, LOW);
  pinMode(PIN_IRQ, INPUT_PULLUP);
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  Serial.begin(115200);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  delay(150);
  configure();
  Serial.printf("\nnRF24 TX test. CONFIG 0x%02X SETUP_AW 0x%02X RF_SETUP 0x%02X (expect 0x02 / 0x03 / 0x27)\n",
                readReg(0x00), readReg(0x03), readReg(0x06));
  help();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c >= '1' && c <= '8') { sendButton(c - '1', holdNext ? holdMs : TAP_MS); holdNext = false; }
  else if (c == 'h' || c == 'H') { holdNext = true; Serial.println("hold: which button (1-8)?"); }
  else if (c == '>') { if (holdMs < 10000) holdMs += 500; help(); }
  else if (c == '<') { if (holdMs > 500) holdMs -= 500; help(); }
  else if (c == ']') { if (pwr < 3) pwr++; configure(); help(); }
  else if (c == '[') { if (pwr > 0) pwr--; configure(); help(); }
  else if (c == '?') help();
}
