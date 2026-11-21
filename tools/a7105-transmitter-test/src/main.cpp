// A7105 AS TRANSMITTER - test if the XL7105-SY-B / MD7105-SY (AMICCOM A7105) can drive the lamp
// 2026-11-21. Board: the A7105 board (same wiring as velua-a7105). Monitor 115200.
//
// The A7105 sends [preamble][ID code][payload] (CRC, FEC and whitening switched off).
// With a 4-byte preamble, ID = CC CC CC CC and payload = B9 A1 99 + 17-byte frame, the
// packet on air should be:   AA AA AA AA | CC CC CC CC | B9 A1 99 + frame
// which matches the remote (and the working CC2500 packet). Timing is the same as the
// CC2500 version: 1245 us slots, 3 packets per channel, channels 2 / 48 / 79.
//
// The open question is frequency deviation. The remote uses about +-250 kHz, the nRF24
// (about +-160 kHz) did not work. TX register II (0x15) holds the deviation setting on the
// A7105; I don't know its exact bit layout, so it can be changed live:
//   ( / )   = reg 0x15 down / up by one, value printed
//   rAAVV   = write register 0xAA = 0xVV (hex), e.g. r1530
//   d       = dump registers 0x00-0x32
//
// Keys: s = automatic sweep | tNN = repeat trial NN | 1-8 tap | h then 1-8 hold | >/< hold time | ? help
#include <Arduino.h>

#define PIN_SCS   10
#define PIN_SCK   12
#define PIN_SDIO  11
#define PIN_GIO1  13

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
static const int PAYLOAD_LEN = 20;   // B9 A1 99 + 17-byte frame
static const uint8_t TX_ID[4] = {0xCC, 0xCC, 0xCC, 0xCC};

// ---- 3-wire SPI, bit-banged (same as velua-a7105) ----
static inline void sckPulseWrite(uint8_t bit) { digitalWrite(PIN_SDIO, bit); digitalWrite(PIN_SCK, HIGH); digitalWrite(PIN_SCK, LOW); }
static void spiWrite(uint8_t v) { pinMode(PIN_SDIO, OUTPUT); for (int i = 7; i >= 0; i--) sckPulseWrite((v >> i) & 1); }
static uint8_t spiRead() {
  pinMode(PIN_SDIO, INPUT);
  uint8_t v = 0;
  for (int i = 7; i >= 0; i--) { digitalWrite(PIN_SCK, HIGH); v = (v << 1) | (digitalRead(PIN_SDIO) & 1); digitalWrite(PIN_SCK, LOW); }
  return v;
}
static inline void csLow()  { digitalWrite(PIN_SCS, LOW); }
static inline void csHigh() { digitalWrite(PIN_SCS, HIGH); }
static void strobe(uint8_t cmd) { csLow(); spiWrite(cmd); csHigh(); }
static void writeReg(uint8_t a, uint8_t v) { csLow(); spiWrite(a & 0x3F); spiWrite(v); csHigh(); }
static uint8_t readReg(uint8_t a) { csLow(); spiWrite(0x40 | (a & 0x3F)); uint8_t v = spiRead(); csHigh(); return v; }
static void writeID(const uint8_t id[4]) { csLow(); spiWrite(0x06); for (int i = 0; i < 4; i++) spiWrite(id[i]); csHigh(); }
static void writeFIFO(const uint8_t *b, int n) { csLow(); spiWrite(0x05); for (int i = 0; i < n; i++) spiWrite(b[i]); csHigh(); }

enum { S_SLEEP=0x80, S_IDLE=0x90, S_STANDBY=0xA0, S_PLL=0xB0, S_RX=0xC0, S_TX=0xD0, S_TXRST=0xE0, S_RXRST=0xF0 };

// Reference A7105 init (same table as velua-a7105); 0xFF = leave at reset.
static const uint8_t INIT[0x33] = {
  0xFF, 0x42, 0x00, 0x14, 0x00, 0xFF, 0xFF, 0x00,  // 00-07
  0x00, 0x00, 0x00, 0x01, 0x21, 0x05, 0x01, 0x50,  // 08-0F
  0x9E, 0x4B, 0x00, 0x02, 0x16, 0x2B, 0x12, 0x4F,  // 10-17
  0x62, 0x80, 0x80, 0x00, 0x0A, 0x32, 0xC3, 0x2F,  // 18-1F
  0x13, 0xC3, 0x00, 0xFF, 0x00, 0x00, 0x3B, 0x00,  // 20-27
  0x17, 0x47, 0x80, 0x03, 0x01, 0x45, 0x18, 0x00,  // 28-2F
  0x01, 0x0F, 0xFF                                  // 30-32
};

static bool calibrate() {
  strobe(S_STANDBY);
  writeReg(0x02, 0x01);                                  // IF filter bank
  uint32_t t = millis();
  while ((readReg(0x02) & 0x01) && millis() - t < 50) {}
  uint8_t ifcal = readReg(0x22);
  writeReg(0x0F, CH[1]);
  strobe(S_PLL);
  writeReg(0x02, 0x06);                                  // VCO bank + current
  t = millis();
  while ((readReg(0x02) & 0x06) && millis() - t < 50) {}
  uint8_t vco = readReg(0x25);
  strobe(S_STANDBY);
  Serial.printf("  calibration: IF %s, VCO %s\n", (ifcal & 0x10) ? "FAIL" : "ok", (vco & 0x08) ? "FAIL" : "ok");
  return !(ifcal & 0x10) && !(vco & 0x08);
}

static uint8_t reg15 = 0x2B;   // TX register II (deviation), start at the reference value

static void radioInit() {
  strobe(S_RXRST); strobe(S_TXRST);
  writeReg(0x00, 0x00);                                  // software reset
  delay(1);
  for (uint8_t a = 0; a < 0x33; a++) if (INIT[a] != 0xFF) writeReg(a, INIT[a]);
  writeReg(0x03, PAYLOAD_LEN - 1);                       // FIFO end pointer -> 20-byte payload
  writeReg(0x0E, 0x01);                                  // 250 kbps (500k / (1+1))
  writeReg(0x1F, 0x07);                                  // Code I: 4-byte ID, 4-byte preamble, no CRC/FEC/whitening
  writeReg(0x15, reg15);
  writeID(TX_ID);
  calibrate();
}

static uint32_t stPkts, stLate, stTimeout, stFirst, stLast, stTxUs;

// The payload goes into the FIFO once per burst; each packet only resets the FIFO read
// pointer and strobes TX (the bit-banged FIFO write was what made v1 late on every packet).
static void sendOne(uint32_t at) {
  strobe(S_TXRST);
  if ((int32_t)(micros() - at) > 50) stLate++;
  while ((int32_t)(micros() - at) < 0) {}
  uint32_t t = micros();
  strobe(S_TX);
  if (stPkts == 0) stFirst = t;
  stLast = t;
  while ((readReg(0x00) & 0x01) && micros() - t < 3000) {}   // TRER clears when the packet is out
  uint32_t dt = micros() - t;
  stTxUs += dt;
  if (dt >= 3000) stTimeout++;
  stPkts++;
}

static void sendFor(const uint8_t *payload, uint32_t ms, int &ch, int &slot, uint32_t &next) {
  strobe(S_STANDBY);
  strobe(S_TXRST);
  writeFIFO(payload, PAYLOAD_LEN);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (slot == 0) { strobe(S_STANDBY); writeReg(0x0F, CH[ch]); }
    sendOne(next);
    next += SLOT_US;
    if (++slot == 3) { slot = 0; ch = (ch + 1) % 3; next += HOP_EXTRA_US; }
  }
}

static uint8_t counter = 0;
static uint32_t holdMs = 2000;
static bool holdNext = false;

// ---- variants for the sweep ----
static uint8_t reg14 = 0x16;      // TX register I; low 3 bits are probably the deviation exponent
static bool invertBits = false;   // send every bit inverted (FSK polarity)
static bool reverseBits = false;  // send every byte LSB first
static uint8_t rev8(uint8_t b) { b = (b & 0xF0) >> 4 | (b & 0x0F) << 4; b = (b & 0xCC) >> 2 | (b & 0x33) << 2; return (b & 0xAA) >> 1 | (b & 0x55) << 1; }
static uint8_t xform(uint8_t b) { if (reverseBits) b = rev8(b); return invertBits ? (uint8_t)~b : b; }

static void applyVariant() {
  strobe(S_STANDBY);
  writeReg(0x14, reg14);
  writeReg(0x15, reg15);
  uint8_t id[4]; for (int i = 0; i < 4; i++) id[i] = xform(TX_ID[i]);
  writeID(id);
}

static void sendButton(int btn, uint32_t pressMs) {
  uint8_t pp[PAYLOAD_LEN] = {0xB9, 0xA1, 0x99}, rp[PAYLOAD_LEN] = {0xB9, 0xA1, 0x99};
  for (int i = 0; i < 17; i++) { pp[3 + i] = PRS[counter][i] ^ BTN_XOR[btn][i]; rp[3 + i] = REL[counter][i]; }
  for (int i = 0; i < PAYLOAD_LEN; i++) { pp[i] = xform(pp[i]); rp[i] = xform(rp[i]); }
  Serial.printf("%s button %d = %s (%lu ms), state %d, reg14=0x%02X reg15=0x%02X%s%s\n",
                pressMs > TAP_MS ? "HOLD" : "TAP ", btn + 1, BTN_NAME[btn], pressMs, counter, reg14, reg15,
                invertBits ? " INVERTED" : "", reverseBits ? " LSB-FIRST" : "");
  stPkts = stLate = stTimeout = stTxUs = 0;
  int ch = 0, slot = 0;
  uint32_t next = micros() + 500;
  sendFor(pp, pressMs, ch, slot, next);
  sendFor(rp, RELEASE_MS, ch, slot, next);
  strobe(S_STANDBY);
  Serial.printf("  sent %lu pkts, avg TX %lu us, avg period %lu us, late %lu, timeouts %lu\n", stPkts,
                stPkts ? stTxUs / stPkts : 0, stPkts > 1 ? (stLast - stFirst) / (stPkts - 1) : 0, stLate, stTimeout);
  counter = (counter + 1) % NSTATES;
}

// ---- automatic sweep: one On/off tap per trial, 4 s apart ----
struct Trial { uint8_t r14, r15; bool inv, rev; };
static Trial trials[64];
static int ntrials = 0;
static void buildTrials() {
  const uint8_t fdp[3] = {5, 6, 7};             // guessed exponent (reg 0x14 bits 2:0)
  const uint8_t fd[4] = {0x07, 0x0F, 0x17, 0x1F};  // guessed mantissa (reg 0x15 bits 4:0)
  ntrials = 0;
  for (int pol = 0; pol < 4; pol++) {           // normal, inverted, LSB-first, both
    bool inv = pol & 1, rev = pol & 2;
    trials[ntrials++] = {0x16, 0x2B, inv, rev}; // reference values first
    for (int e = 0; e < 3; e++)
      for (int m = 0; m < 4; m++)
        trials[ntrials++] = {(uint8_t)((0x16 & 0xF8) | fdp[e]), (uint8_t)((0x2B & 0xE0) | fd[m]), inv, rev};
  }
}
static void runTrial(int i) {
  reg14 = trials[i].r14; reg15 = trials[i].r15; invertBits = trials[i].inv; reverseBits = trials[i].rev;
  applyVariant();
  Serial.printf("\n##### TRIAL %d of %d #####\n", i + 1, ntrials);
  sendButton(0, TAP_MS);
}
static bool keyPressed() { if (Serial.available()) { while (Serial.available()) Serial.read(); return true; } return false; }
static void sweep() {
  buildTrials();
  Serial.printf("SWEEP: %d trials, one On/off tap every 4 s (~%d min). Starts in 20 s - go to the lamp.\n"
                "Note the time on your phone every time the lamp flicks. Any key stops the sweep.\n",
                ntrials, ntrials * 4 / 60 + 1);
  for (int s = 20; s > 0; s--) { Serial.printf("  starting in %d\n", s); delay(1000); if (keyPressed()) return; }
  for (int i = 0; i < ntrials; i++) {
    uint32_t t0 = millis();
    runTrial(i);
    while (millis() - t0 < 4000) if (keyPressed()) { Serial.println("sweep stopped"); return; }
  }
  Serial.println("SWEEP DONE");
}

static void help() {
  Serial.printf("A7105 TX test: s = SWEEP | tNN = repeat trial NN | 1-8 tap | h+1-8 hold %lu ms | ( ) reg15=0x%02X | rAAVV | d dump\n",
                holdMs, reg15);
}

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

void setup() {
  pinMode(PIN_SCS, OUTPUT); digitalWrite(PIN_SCS, HIGH);
  pinMode(PIN_SCK, OUTPUT); digitalWrite(PIN_SCK, LOW);
  pinMode(PIN_SDIO, OUTPUT);
  pinMode(PIN_GIO1, INPUT);
  Serial.begin(115200);
  delay(1500);
  strobe(S_RXRST);
  writeReg(0x00, 0x00); delay(1);
  const uint8_t probe[4] = {0xB9, 0xA1, 0x99, 0x0D};
  writeID(probe);
  uint8_t rb[4]; csLow(); spiWrite(0x40 | 0x06); for (int i = 0; i < 4; i++) rb[i] = spiRead(); csHigh();
  bool ok = rb[0] == 0xB9 && rb[1] == 0xA1 && rb[2] == 0x99 && rb[3] == 0x0D;
  Serial.printf("\nA7105 TX test. SPI round-trip %s\n", ok ? "OK" : "FAIL (check wiring)");
  radioInit();
  help();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c >= '1' && c <= '8') { sendButton(c - '1', holdNext ? holdMs : TAP_MS); holdNext = false; }
  else if (c == 'h' || c == 'H') { holdNext = true; Serial.println("hold: which button (1-8)?"); }
  else if (c == '>') { if (holdMs < 10000) holdMs += 500; help(); }
  else if (c == '<') { if (holdMs > 500) holdMs -= 500; help(); }
  else if (c == '(' || c == ')') {
    reg15 = (reg15 + (c == ')' ? 1 : -1)) & 0xFF;
    writeReg(0x15, reg15);
    Serial.printf("reg 0x15 = 0x%02X (read back 0x%02X)\n", reg15, readReg(0x15));
  }
  else if (c == 'r') {                                   // rAAVV
    char h[4]; uint32_t t = millis(); int n = 0;
    while (n < 4 && millis() - t < 5000) if (Serial.available()) h[n++] = Serial.read();
    int a = n == 4 ? hexv(h[0]) * 16 + hexv(h[1]) : -1, v = n == 4 ? hexv(h[2]) * 16 + hexv(h[3]) : -1;
    if (a < 0 || v < 0 || a > 0x32) { Serial.println("usage: rAAVV, e.g. r1530"); return; }
    writeReg(a, v);
    if (a == 0x15) reg15 = v;
    Serial.printf("reg 0x%02X = 0x%02X (read back 0x%02X)\n", a, v, readReg(a));
  }
  else if (c == 'd') {
    for (int a = 0; a <= 0x32; a++) Serial.printf("%02X:%02X%s", a, readReg(a), (a % 8 == 7) ? "\n" : " ");
    Serial.println();
  }
  else if (c == 's' || c == 'S') sweep();
  else if (c == 't' || c == 'T') {                       // tNN = run trial NN once
    char h[2]; uint32_t t = millis(); int n = 0;
    while (n < 2 && millis() - t < 5000) if (Serial.available()) h[n++] = Serial.read();
    if (!ntrials) buildTrials();
    int k = (n == 2 && isdigit(h[0]) && isdigit(h[1])) ? (h[0] - '0') * 10 + (h[1] - '0') : 0;
    if (k < 1 || k > ntrials) { Serial.printf("usage: tNN, NN = 01..%d\n", ntrials); return; }
    runTrial(k - 1);
  }
  else if (c == '?') help();
}
