// TRANSMIT BOARD (ESP32-S3 + CC2500PA) - v10: all 8 buttons, tap + hold (v9 preamble/timing)   2026-11-19
// Project: nrf24-antenna-test -> src/main.cpp   (monitor 115200)
//
// What the raw CC2500 capture of the REAL remote showed (velua-cc2500-capture log 202648):
//  * On air before the sync word:  ...10101010 | 11001100 x4 | B9 A1 99 | 17-byte frame
//    i.e. a real 1010 preamble (0xAA) and then 0xCC x4, byte-aligned to the sync word.
//    Our v3..v8 packet started "1001100 1100 1100 ..." - it had NO 1010 preamble at all.
//  * Nothing fixed follows the 17-byte frame (bits after it are noise) -> no trailer.
//  * Timing: packets every ~1245 us, 3 per channel, full hop cycle ~11.69 ms.
//    v8 sent one every ~1981 us plus ~0.8 ms recalibration at every hop.
//
// v9: packet = AA AA AA AA CC CC CC CC B9 A1 99 + frame (28 bytes, byte-aligned).
//     Packets go out on a fixed 1245 us grid, 3 per channel, +160 us at each hop.
//     Each channel is calibrated ONCE at boot; hopping just restores the saved FSCAL values
//     (the standard CC2500 fast-hopping method), so hops no longer cost a calibration.
//     (A/B 2026-11-19: the old v8 packet brought the misses back -> preamble was the root cause.)
//
// Serial: see the UI section (v10: 1-8 tap, h+1-8 hold).
// One tap (press burst + release burst) = ONE lamp toggle. The counter does not gate it.
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>

#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CSN   10
#define PIN_GDO0   4
#define PIN_GDO2   5
#define PIN_PA     6
#define PIN_LNA    7
#define PIN_BTN    0

// 17-byte frames as transmitted after B9 A1 99 (see docs/protocol.md, bit-exact)
static const int NSTATES = 8;
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
static const char *CNT[NSTATES] = {"B922","A832","A911","B801","8A02","9B12","9A31","8B21"};

// v10: ALL 8 BUTTONS. Recorded 2026-11-19 (evidence/04_nrf24_all-8-buttons.log; buttons numbered left to right as in docs/protocol.md).
// Every button's PRESS frame = On/off PRESS frame (PRS above) XOR a constant per button - verified
// identical across 3-5 captured counter states per button, so all 8 states are valid for all 8
// buttons. The RELEASE frame (REL above) is the SAME for every button. A hold is just the press
// frame repeated for as long as the button is held, then the normal release.
static const int NBTN = 8;
static const uint8_t BTN_XOR[NBTN][17] = {
  {0,0,0,0,0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  // 1 = On/off
  {0,0,0,0,0,0x10,0x22,0x80,0x88,0x00,0x00,0x10,0x22,0x49,0x97,0xEA,0x0C},  // 2
  {0,0,0,0,0,0x20,0x77,0x80,0x88,0x00,0x00,0x20,0x77,0x94,0x16,0x19,0x89},  // 3
  {0,0,0,0,0,0x33,0x31,0x80,0x88,0x00,0x00,0x33,0x31,0xB7,0x14,0xDA,0xA9},  // 4
  {0,0,0,0,0,0x40,0xDD,0x80,0x88,0x00,0x00,0x40,0xDD,0xC0,0x44,0xAF,0x4E},  // 5
  {0,0,0,0,0,0x00,0x11,0x80,0x99,0x80,0x88,0x00,0x00,0x40,0xDD,0x5C,0x3B},  // 6
  {0,0,0,0,0,0x00,0x11,0x90,0xBB,0x00,0x00,0x10,0x22,0x68,0xD2,0x24,0x3B},  // 7
  {0,0,0,0,0,0x00,0x11,0x91,0x98,0x88,0x00,0x11,0x01,0x5D,0xE8,0x8B,0x94},  // 8
};
static const char *BTN_NAME[NBTN] = {
  "On/off", "Night (lowest dim)", "Brightness up", "Brightness down",
  "Cycle 3 color temps", "Timer 60 s", "Color temp cooler", "Color temp warmer"};
static void pressFrame(int btn, int s, uint8_t *f) { for (int i = 0; i < 17; i++) f[i] = PRS[s][i] ^ BTN_XOR[btn][i]; }
static const uint32_t PRESS_MS = 100, RELEASE_MS = 1600;

static const float    CH_MHZ[3] = {2402.00, 2448.00, 2478.95};
static const int      PKTS_PER_CH = 3;
static const uint32_t SLOT_US = 1245;     // remote: packet-to-packet on one channel
static const uint32_t HOP_EXTRA_US = 160; // remote: 11.69 ms cycle - 9 x 1245

static const int MAXPKT = 32;
static uint8_t pktLen = 0;

// --- packet builders ---------------------------------------------------------------------
static int buildNew(const uint8_t *f, uint8_t *o) {
  static const uint8_t head[11] = {0xAA,0xAA,0xAA,0xAA, 0xCC,0xCC,0xCC,0xCC, 0xB9,0xA1,0x99};
  memcpy(o, head, 11); memcpy(o + 11, f, 17);
  return 28;
}

// --- CC2500 low level ----------------------------------------------------------------------
SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);
static void csLow() { digitalWrite(PIN_CSN, LOW); uint32_t t = micros(); while (digitalRead(PIN_MISO) && micros() - t < 1000) {} }
static void csHigh() { digitalWrite(PIN_CSN, HIGH); }
static void strobe(uint8_t s) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(s); csHigh(); SPI.endTransaction(); }
static void writeReg(uint8_t a, uint8_t v) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a); SPI.transfer(v); csHigh(); SPI.endTransaction(); }
static uint8_t readReg(uint8_t a) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a | 0x80); uint8_t v = SPI.transfer(0); csHigh(); SPI.endTransaction(); return v; }
static uint8_t readStatus(uint8_t a) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a | 0xC0); uint8_t v = SPI.transfer(0); csHigh(); SPI.endTransaction(); return v; }
static void writeFifo(const uint8_t *b, uint8_t n) {
  SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(0x7F);
  for (uint8_t i = 0; i < n; i++) SPI.transfer(b[i]);
  csHigh(); SPI.endTransaction();
}

enum : uint8_t {
  IOCFG2 = 0x00, IOCFG0 = 0x02, FIFOTHR = 0x03, PKTLEN = 0x06,
  PKTCTRL1 = 0x07, PKTCTRL0 = 0x08, ADDR = 0x09, CHANNR = 0x0A,
  FSCTRL1 = 0x0B, FSCTRL0 = 0x0C, FREQ2 = 0x0D, FREQ1 = 0x0E, FREQ0 = 0x0F,
  MDMCFG4 = 0x10, MDMCFG3 = 0x11, MDMCFG2 = 0x12, MDMCFG1 = 0x13, MDMCFG0 = 0x14,
  DEVIATN = 0x15, MCSM1 = 0x17, MCSM0 = 0x18, FOCCFG = 0x19, BSCFG = 0x1A,
  AGCCTRL2 = 0x1B, AGCCTRL1 = 0x1C, AGCCTRL0 = 0x1D, FREND1 = 0x21, FREND0 = 0x22,
  FSCAL3 = 0x23, FSCAL2 = 0x24, FSCAL1 = 0x25, FSCAL0 = 0x26,
  TEST2 = 0x2C, TEST1 = 0x2D, TEST0 = 0x2E, PATABLE = 0x3E,
  SRES = 0x30, SFSTXON = 0x31, SCAL = 0x33, STX = 0x35, SIDLE = 0x36, SFTX = 0x3B,
  MARCSTATE = 0x35
};
static uint8_t state() { return readStatus(MARCSTATE) & 0x1F; }
static void waitState(uint8_t s, uint32_t us) { uint32_t t = micros(); while (state() != s && micros() - t < us) {} }

static void radioReset() {
  csHigh(); delayMicroseconds(30);
  digitalWrite(PIN_CSN, LOW); delayMicroseconds(30);
  csHigh(); delayMicroseconds(50);
  strobe(SRES);
  delay(2);
}

static void radioConfig() {
  writeReg(IOCFG2,   0x29);
  writeReg(IOCFG0,   0x06);
  writeReg(FIFOTHR,  0x07);
  writeReg(PKTCTRL1, 0x00);
  writeReg(PKTCTRL0, 0x00);   // fixed length, no CRC, no whitening, FIFO mode
  writeReg(ADDR,     0x00);
  writeReg(CHANNR,   0x00);
  writeReg(FSCTRL1,  0x10);
  writeReg(FSCTRL0,  0x00);
  writeReg(MDMCFG4,  0x0D);   // DRATE_E 13
  writeReg(MDMCFG3,  0x3B);   // -> 249.9 kbps
  writeReg(MDMCFG2,  0x00);   // 2-FSK, chip adds no preamble/sync (ours is in the packet)
  writeReg(MDMCFG1,  0x22);
  writeReg(MDMCFG0,  0xF8);
  writeReg(DEVIATN,  0x73);   // ~279 kHz
  writeReg(MCSM1,    0x31);   // after TX -> FSTXON (v8 fix; 0x32 kept the carrier on)
  writeReg(MCSM0,    0x08);   // no autocal; channels pre-calibrated at boot
  writeReg(FOCCFG,   0x1D);
  writeReg(BSCFG,    0x1C);
  writeReg(AGCCTRL2, 0xC7);
  writeReg(AGCCTRL1, 0x40);
  writeReg(AGCCTRL0, 0xB2);
  writeReg(FREND1,   0xB6);
  writeReg(FREND0,   0x10);
  writeReg(FSCAL3,   0xEA);
  writeReg(FSCAL2,   0x0A);
  writeReg(FSCAL1,   0x00);
  writeReg(FSCAL0,   0x11);
  writeReg(TEST2,    0x88);
  writeReg(TEST1,    0x31);
  writeReg(TEST0,    0x0B);
}

// per-channel frequency words and calibration results (fast hopping)
static uint32_t chWord[3];
static uint8_t  chCal[3][3];     // FSCAL3, FSCAL2, FSCAL1

static void writeFreq(uint32_t w) {
  writeReg(FREQ2, (w >> 16) & 0xFF); writeReg(FREQ1, (w >> 8) & 0xFF); writeReg(FREQ0, w & 0xFF);
}
static void calibrateAll() {
  for (int i = 0; i < 3; i++) {
    chWord[i] = (uint32_t)((double)CH_MHZ[i] * 1e6 * 65536.0 / 26e6 + 0.5);
    strobe(SIDLE); waitState(0x01, 2000);
    writeFreq(chWord[i]);
    strobe(SCAL); delayMicroseconds(50); waitState(0x01, 3000);
    chCal[i][0] = readReg(FSCAL3); chCal[i][1] = readReg(FSCAL2); chCal[i][2] = readReg(FSCAL1);
    Serial.printf("  cal %.2f MHz: FSCAL3=%02X FSCAL2=%02X FSCAL1=%02X\n", CH_MHZ[i], chCal[i][0], chCal[i][1], chCal[i][2]);
  }
}
static void hopTo(int i) {           // no calibration: restore saved values, re-lock synthesizer
  strobe(SIDLE); waitState(0x01, 500);
  writeFreq(chWord[i]);
  writeReg(FSCAL3, chCal[i][0]); writeReg(FSCAL2, chCal[i][1]); writeReg(FSCAL1, chCal[i][2]);
  strobe(SFSTXON);
}

// --- transmit --------------------------------------------------------------------------------
static uint32_t stPkts, stLate, stTimeout, stUnder, stTxUsSum, stFirstUs, stLastUs;

static void sendOne(const uint8_t *pkt, uint32_t at) {
  writeFifo(pkt, pktLen);
  if ((int32_t)(micros() - at) > 50) stLate++;
  while ((int32_t)(micros() - at) < 0) {}
  uint32_t t = micros();
  strobe(STX);
  if (stPkts == 0) stFirstUs = t;
  stLastUs = t;
  while (state() != 0x13 && micros() - t < 400) {}     // into TX
  while (state() == 0x13 && micros() - t < 3000) {}    // until done
  uint32_t dt = micros() - t;
  stTxUsSum += dt; stPkts++;
  if (dt >= 3000) stTimeout++;
  if (state() == 0x16) { stUnder++; strobe(SIDLE); strobe(SFTX); strobe(SFSTXON); }
}

// one burst of `ms` with the remote's slot timing; ch/slot carry over between bursts
static void sendFor(const uint8_t *pkt, uint32_t ms, int &ch, int &slot, uint32_t &next) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (slot == 0) hopTo(ch);
    sendOne(pkt, next);
    next += SLOT_US;
    if (++slot == PKTS_PER_CH) { slot = 0; ch = (ch + 1) % 3; next += HOP_EXTRA_US; }
  }
}

static void ampOff() { digitalWrite(PIN_PA, LOW); digitalWrite(PIN_LNA, LOW); }
static void ampTx()  { digitalWrite(PIN_PA, HIGH); digitalWrite(PIN_LNA, LOW); }
static void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_BUILTIN, r, g, b); }

// one button action: press frame for pressMs (100 = tap, longer = hold), then the release burst
static void sendButton(int btn, int s, uint32_t pressMs) {
  uint8_t f[17], pp[MAXPKT], rp[MAXPKT];
  pressFrame(btn, s, f);
  pktLen = buildNew(f, pp);
  buildNew(REL[s], rp);
  writeReg(PKTLEN, pktLen);
  ampTx(); led(40, 0, 0);
  stPkts = stLate = stTimeout = stUnder = stTxUsSum = 0;
  int ch = 0, slot = 0;
  uint32_t next = micros() + 300;
  sendFor(pp, pressMs, ch, slot, next);
  sendFor(rp, RELEASE_MS, ch, slot, next);
  strobe(SIDLE); ampOff(); led(0, 0, 0);
  Serial.printf("  sent: %lu pkts, avg period %lu us, late %lu, timeouts %lu, underflows %lu\n",
                stPkts, stPkts > 1 ? (stLastUs - stFirstUs) / (stPkts - 1) : 0, stLate, stTimeout, stUnder);
}

// --- UI ----------------------------------------------------------------------------------------
// 1-8      = TAP that button (1 = On/off)
// h then 1-8 = HOLD that button for holdMs (default 2000 ms), then release
// > / <    = hold time +/- 500 ms           ] / [ = power up/down       ? = help
// Keys typed while the LED is red are queued and run afterwards.
Preferences prefs;
static uint8_t ptr = 0;               // shared 8-state counter, advances on every action like the remote
static const uint8_t PWR[] = {0x00, 0x1D, 0x46, 0x60, 0x84, 0xA0, 0xC0, 0xE0, 0xFE};
static const int NPWR = sizeof(PWR);
static int pwr = 3;
static uint32_t holdMs = 2000;
static bool holdNext = false;

static void applyPwr() {
  writeReg(PATABLE, PWR[pwr]); prefs.putInt("pwr2", pwr);
  Serial.printf("power = PATABLE 0x%02X (level %d/%d)\n", PWR[pwr], pwr + 1, NPWR);
}
static void help() {
  Serial.printf("v10: 1-8 = tap button | h+1-8 = hold button %lu ms | >/< hold time | ]/[ power (level %d, 0x%02X)\n",
                holdMs, pwr + 1, PWR[pwr]);
}
static void action(int btn, bool hold) {
  uint32_t ms = hold ? holdMs : PRESS_MS;
  Serial.printf("%s button %d = %s (%lu ms), state %d (cnt %s)\n", hold ? "HOLD" : "TAP ", btn + 1, BTN_NAME[btn], ms, ptr, CNT[ptr]);
  sendButton(btn, ptr, ms);
  ptr = (ptr + 1) % NSTATES;
  prefs.putUChar("ptr", ptr);
}

void setup() {
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_PA, OUTPUT); pinMode(PIN_LNA, OUTPUT); ampOff();
  pinMode(PIN_GDO0, INPUT); pinMode(PIN_GDO2, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  Serial.begin(115200);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  radioReset();
  Serial.printf("\nTRANSMIT BOARD v10 - all 8 buttons, tap + hold. PARTNUM 0x%02X (expect 0x80)\n", readStatus(0x30));
  radioConfig();
  calibrateAll();
  prefs.begin("velua", false);
  ptr = prefs.getUChar("ptr", 0) % NSTATES;
  pwr = constrain(prefs.getInt("pwr2", 3), 0, NPWR - 1);
  applyPwr();
  for (int i = 0; i < 3; i++) { led(0, 0, 40); delay(300); led(0, 0, 0); delay(300); }
  help();
}

void loop() {
  static bool btnWas = HIGH;
  bool btn = digitalRead(PIN_BTN);
  if (btnWas == HIGH && btn == LOW) {
    delay(20);
    if (digitalRead(PIN_BTN) == LOW) { Serial.println("[BOOT button]"); action(0, false); }
    while (digitalRead(PIN_BTN) == LOW) delay(5);
  }
  btnWas = btn;

  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == '\n' || c == '\r' || c == ' ' || c == '\t') return;
  if (c >= '1' && c <= '8') { action(c - '1', holdNext); holdNext = false; }
  else if (c == 'h' || c == 'H') { holdNext = true; Serial.printf("hold: which button (1-8)? %lu ms\n", holdMs); }
  else if (c == '>') { if (holdMs < 10000) holdMs += 500; help(); }
  else if (c == '<') { if (holdMs > 500) holdMs -= 500; help(); }
  else if (c == ']') { if (pwr < NPWR - 1) pwr++; applyPwr(); }
  else if (c == '[') { if (pwr > 0) pwr--; applyPwr(); }
  else if (c == '?') help();
  else holdNext = false;
}
