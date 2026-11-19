// RAW CAPTURE (ESP32-S3 + CC2500PA, the TX board) - 2026-11-19 v2   monitor 921600
// Project: velua-cc2500-capture -> src/main.cpp      (v1 kept as ../main_v1.cpp.txt)
//
// Goal: see what the REAL remote puts on air BEFORE and AFTER the B9 A1 99 sync, which the
// nRF24 strips: the true preamble, the trailer and whatever comes after the 17-byte frame.
// Also: the remote's packet spacing on one channel and its frequency offset vs our CC2500.
//
// v2: v1 got 0 captures (carrier-sense start never fired). Default is now FREE-RUNNING RX
// (SYNC_MODE=0: back-to-back 48-byte windows), a window is kept only if the peak RSSI seen
// during it passes a gate. 'r' shows a live RSSI bar graph to prove the radio hears the remote.
//
// Keys: c = capture 4 s window (press the remote's On/off ONCE after "GO")
//       r = RSSI monitor 4 s (peak per 100 ms) - press the remote during it
//       g/h/k = channel 2402 / 2448 / 2478.95 MHz      ]/[ = RSSI gate +/-5 dB
//       m = mode free-running <-> carrier-sense         }/{ = carrier-sense threshold
//       x = raw hex dump of kept captures that had no B9A199
#include <Arduino.h>
#include <SPI.h>

#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CSN   10
#define PIN_GDO0   4
#define PIN_GDO2   5
#define PIN_PA     6
#define PIN_LNA    7

static const int CAP_LEN = 48;          // bytes per window (384 bits, ~1.5 ms)
static const int NB = CAP_LEN * 8;
static const int MAXCAP = 700;
static const uint32_t WINDOW_MS = 4000;

struct Cap { uint32_t t; int8_t fe; int8_t peak; uint8_t rssi, lqi; uint8_t d[CAP_LEN]; };
static Cap caps[MAXCAP];
static int ncap = 0;
static int syncMode = 0;           // 0 = free-running, 4 = carrier-sense start
static int rssiGate = -65;         // dBm (CC2500 scale; CC2591 LNA adds gain -> relative only)
static uint32_t rawCaps; static int peakAll;

// The 16 known frames (17 bytes after B9 A1 99) - see docs/protocol.md, bit-exact.
static const uint8_t REL[8][17] = {
  {0x0D,0xBF,0x29,0xB9,0x22,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xED,0x99,0x1E,0xA7,0x3E,0xAC},
  {0x0D,0xBF,0x29,0xA8,0x32,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xFC,0x89,0xA0,0x18,0xB2,0xF1},
  {0x0D,0xBF,0x29,0xA9,0x11,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xFD,0xAA,0xA1,0xDA,0x63,0xB4},
  {0x0D,0xBF,0x29,0xB8,0x01,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xEC,0xBA,0x1F,0x65,0xEF,0xE9},
  {0x0D,0xBF,0x29,0x8A,0x02,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xDE,0xB9,0xC3,0x26,0x40,0x46},
  {0x0D,0xBF,0x29,0x9B,0x12,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xCF,0xA9,0x7D,0x99,0xCC,0x1B},
  {0x0D,0xBF,0x29,0x9A,0x31,0x57,0x5E,0x4B,0x9C,0xA1,0xB6,0xCE,0x8A,0x7C,0x5B,0x1D,0x5E},
  {0x0D,0xBF,0x29,0x8B,0x21,0xDF,0x5E,0x4B,0x9C,0xA1,0xB6,0xDF,0x9A,0xC2,0xE4,0x91,0x03},
};
static const uint8_t PRS[8][17] = {
  {0x0D,0xBF,0x29,0x8B,0x30,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xDF,0x9A,0x4D,0xDD,0x6B,0x2D},
  {0x0D,0xBF,0x29,0xB9,0x33,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xED,0x99,0x91,0x9E,0xC4,0x82},
  {0x0D,0xBF,0x29,0xA8,0x23,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xFC,0x89,0x2F,0x21,0x48,0xDF},
  {0x0D,0xBF,0x29,0xA9,0x00,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xFD,0xAA,0x2E,0xE3,0x99,0x9A},
  {0x0D,0xBF,0x29,0xB8,0x10,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xEC,0xBA,0x90,0x5C,0x15,0xC7},
  {0x0D,0xBF,0x29,0x8A,0x13,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xDE,0xB9,0x4C,0x1F,0xBA,0x68},
  {0x0D,0xBF,0x29,0x9B,0x03,0x5F,0xC7,0xCB,0x14,0xA1,0xB6,0xCF,0xA9,0xF2,0xA0,0x36,0x35},
  {0x0D,0xBF,0x29,0x9A,0x20,0xD7,0xC7,0xCB,0x14,0xA1,0xB6,0xCE,0x8A,0xF3,0x62,0xE7,0x70},
};

SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);
static void csLow() { digitalWrite(PIN_CSN, LOW); uint32_t t = micros(); while (digitalRead(PIN_MISO) && micros() - t < 1000) {} }
static void csHigh() { digitalWrite(PIN_CSN, HIGH); }
static void strobe(uint8_t s) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(s); csHigh(); SPI.endTransaction(); }
static void writeReg(uint8_t a, uint8_t v) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a); SPI.transfer(v); csHigh(); SPI.endTransaction(); }
static uint8_t readStatus(uint8_t a) { SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a | 0xC0); uint8_t v = SPI.transfer(0); csHigh(); SPI.endTransaction(); return v; }
static void readFifo(uint8_t *b, int n) {
  SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(0xFF);      // burst read RX FIFO
  for (int i = 0; i < n; i++) b[i] = SPI.transfer(0);
  csHigh(); SPI.endTransaction();
}

enum : uint8_t {
  IOCFG2 = 0x00, IOCFG0 = 0x02, FIFOTHR = 0x03, PKTLEN = 0x06, PKTCTRL1 = 0x07, PKTCTRL0 = 0x08,
  ADDR = 0x09, CHANNR = 0x0A, FSCTRL1 = 0x0B, FSCTRL0 = 0x0C, FREQ2 = 0x0D, FREQ1 = 0x0E, FREQ0 = 0x0F,
  MDMCFG4 = 0x10, MDMCFG3 = 0x11, MDMCFG2 = 0x12, MDMCFG1 = 0x13, MDMCFG0 = 0x14, DEVIATN = 0x15,
  MCSM1 = 0x17, MCSM0 = 0x18, FOCCFG = 0x19, BSCFG = 0x1A, AGCCTRL2 = 0x1B, AGCCTRL1 = 0x1C,
  AGCCTRL0 = 0x1D, FREND1 = 0x21, FREND0 = 0x22, FSCAL3 = 0x23, FSCAL2 = 0x24, FSCAL1 = 0x25,
  FSCAL0 = 0x26, TEST2 = 0x2C, TEST1 = 0x2D, TEST0 = 0x2E,
  SRES = 0x30, SCAL = 0x33, SRX = 0x34, SIDLE = 0x36, SFRX = 0x3A,
  FREQEST = 0x32, RSSIREG = 0x34, MARCSTATE = 0x35, RXBYTES = 0x3B
};
static uint8_t marc() { return readStatus(MARCSTATE) & 0x1F; }
static int rssiDbm() { int8_t r = (int8_t)readStatus(RSSIREG); return r / 2 - 72; }

static const float CH_MHZ[3] = {2402.00, 2448.00, 2478.95};
static int chIdx = 0;
static int csThr = 6;            // carrier-sense absolute threshold, dB above MAGN_TARGET (1..7)

static void applyThr() {
  writeReg(AGCCTRL1, 0x40 | (csThr & 0x0F));
  Serial.printf("carrier-sense threshold = +%d dB\n", csThr);
}
static void applyMode() { writeReg(MDMCFG2, syncMode == 4 ? 0x04 : 0x00); }

static void radioConfig() {
  writeReg(IOCFG2, 0x0E);        // GDO2 = carrier sense
  writeReg(IOCFG0, 0x06);
  writeReg(FIFOTHR, 0x07);
  writeReg(PKTLEN, CAP_LEN);
  writeReg(PKTCTRL1, 0x04);      // append RSSI + LQI, no address check
  writeReg(PKTCTRL0, 0x00);      // fixed length, no CRC, no whitening
  writeReg(ADDR, 0x00);
  writeReg(CHANNR, 0x00);
  writeReg(FSCTRL1, 0x10);
  writeReg(FSCTRL0, 0x00);
  writeReg(MDMCFG4, 0x0D);       // RX BW 812 kHz, DRATE_E 13
  writeReg(MDMCFG3, 0x3B);       // 249.9 kbps
  applyMode();                   // 2-FSK, no sync word
  writeReg(MDMCFG1, 0x22);
  writeReg(MDMCFG0, 0xF8);
  writeReg(DEVIATN, 0x73);
  writeReg(MCSM1, 0x30);         // after RX -> IDLE
  writeReg(MCSM0, 0x08);         // manual calibration
  writeReg(FOCCFG, 0x1E);        // freq offset comp limit +-BW/4 (~+-200 kHz) so FREQEST can see it
  writeReg(BSCFG, 0x1C);
  writeReg(AGCCTRL2, 0xC7);
  applyThr();
  writeReg(AGCCTRL0, 0xB2);
  writeReg(FREND1, 0xB6);
  writeReg(FREND0, 0x10);
  writeReg(FSCAL3, 0xEA); writeReg(FSCAL2, 0x0A); writeReg(FSCAL1, 0x00); writeReg(FSCAL0, 0x11);
  writeReg(TEST2, 0x88); writeReg(TEST1, 0x31); writeReg(TEST0, 0x0B);
}

static void idle() { strobe(SIDLE); uint32_t t = micros(); while (marc() != 0x01 && micros() - t < 2000) {} }

static void setChannel(int i) {
  chIdx = i;
  idle();
  uint32_t w = (uint32_t)((double)CH_MHZ[i] * 1e6 * 65536.0 / 26e6 + 0.5);
  writeReg(FREQ2, (w >> 16) & 0xFF); writeReg(FREQ1, (w >> 8) & 0xFF); writeReg(FREQ0, w & 0xFF);
  strobe(SCAL);
  uint32_t t = micros(); while (marc() != 0x01 && micros() - t < 2000) {}
  Serial.printf("channel = %.2f MHz\n", CH_MHZ[i]);
}

static void startRx() { idle(); strobe(SFRX); strobe(SRX); }

static void captureWindow() {
  ncap = 0; rawCaps = 0; peakAll = -200;
  uint32_t ovf = 0, tmo = 0;
  digitalWrite(PIN_PA, LOW); digitalWrite(PIN_LNA, HIGH);   // RX = LNA on
  Serial.printf("\n>>> GO - press the remote's On/off ONCE now (%lu ms, %.2f MHz, %s, gate %d dBm)\n",
                WINDOW_MS, CH_MHZ[chIdx], syncMode == 4 ? "carrier-sense" : "free-running", rssiGate);
  uint32_t t0 = millis();
  startRx();
  Serial.printf("    MARCSTATE after SRX = 0x%02X (0x0D = RX)\n", marc());
  uint32_t ts = micros(); int peak = -200;
  while (millis() - t0 < WINDOW_MS && ncap < MAXCAP) {
    int r = rssiDbm(); if (r > peak) peak = r;
    uint8_t n = readStatus(RXBYTES);
    if (n & 0x80) { ovf++; startRx(); ts = micros(); peak = -200; continue; }
    if ((n & 0x7F) < CAP_LEN + 2) {
      if (syncMode == 4 && (n & 0x7F) == 0) ts = micros();            // not started yet
      else if (micros() - ts > 5000) { tmo++; startRx(); ts = micros(); peak = -200; }
      continue;
    }
    rawCaps++;
    if (peak > peakAll) peakAll = peak;
    if (peak >= rssiGate) {
      Cap &c = caps[ncap];
      c.t = ts; c.peak = peak;
      c.fe = (int8_t)readStatus(FREQEST);
      uint8_t st[2];
      readFifo(c.d, CAP_LEN); readFifo(st, 2);
      c.rssi = st[0]; c.lqi = st[1];
      ncap++;
    }
    startRx(); ts = micros(); peak = -200;
  }
  idle();
  digitalWrite(PIN_LNA, LOW);
  Serial.printf("<<< window closed: %lu raw windows, %d kept (peak >= %d dBm), loudest %d dBm, overflows %lu, timeouts %lu%s\n",
                rawCaps, ncap, rssiGate, peakAll, ovf, tmo, ncap >= MAXCAP ? "  BUFFER FULL (raise gate with ])" : "");
}

static void rssiMonitor() {
  digitalWrite(PIN_PA, LOW); digitalWrite(PIN_LNA, HIGH);
  writeReg(MDMCFG2, 0x00);
  writeReg(PKTCTRL0, 0x02);      // infinite length: RX never ends
  idle(); strobe(SFRX); strobe(SRX);
  Serial.printf(">>> RSSI monitor 4 s at %.2f MHz - press the remote now. MARCSTATE=0x%02X (0x0D = RX)\n",
                CH_MHZ[chIdx], marc());
  for (int i = 0; i < 40; i++) {
    int pk = -200; uint32_t t = millis();
    while (millis() - t < 100) {
      int r = rssiDbm(); if (r > pk) pk = r;
      if (readStatus(RXBYTES) & 0xC0) { idle(); strobe(SFRX); strobe(SRX); }   // keep FIFO from overflowing
    }
    int bars = constrain((pk + 110) / 2, 0, 50);
    Serial.printf("%4d dBm |", pk); for (int b = 0; b < bars; b++) Serial.print('#'); Serial.println();
  }
  idle(); strobe(SFRX);
  writeReg(PKTCTRL0, 0x00);
  applyMode();
  digitalWrite(PIN_LNA, LOW);
}

static inline int bitAt(const uint8_t *d, int i) { return (d[i >> 3] >> (7 - (i & 7))) & 1; }
static uint8_t byteAt(const uint8_t *d, int bit) { uint8_t v = 0; for (int k = 0; k < 8; k++) v = (v << 1) | bitAt(d, bit + k); return v; }

// returns sync bit offset or -1; inv=1 if found inverted
static int findSync(const uint8_t *d, int &inv) {
  uint32_t w = 0;
  for (int i = 0; i < NB; i++) {
    w = ((w << 1) | bitAt(d, i)) & 0xFFFFFF;
    if (i >= 23) {
      if (w == 0xB9A199) { inv = 0; return i - 23; }
      if (w == (~0xB9A199u & 0xFFFFFF)) { inv = 1; return i - 23; }
    }
  }
  return -1;
}

static void identify(const uint8_t *f, char *out) {
  int best = 999; char kind = '?'; int bi = -1;
  for (int s = 0; s < 8; s++) for (int k = 0; k < 2; k++) {
    const uint8_t *ref = k ? PRS[s] : REL[s];
    int e = 0; for (int b = 0; b < 17; b++) e += __builtin_popcount(ref[b] ^ f[b]);
    if (e < best) { best = e; kind = k ? 'P' : 'R'; bi = s; }
  }
  if (best == 0) sprintf(out, "%c%d ok", kind, bi);
  else sprintf(out, "%c%d? %dbiterr", kind, bi, best);
}

static int cmpI32(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void analyze() {
  int matches = 0, invs = 0; uint32_t lastT = 0;   // lastT = estimated sync time (us)
  static int fes[MAXCAP]; int nfe = 0;
  Serial.println("idx  dt_us  peak fe_kHz off | 40 bits BEFORE sync (oldest..newest)     | frame          | bits AFTER the 17-byte frame");
  for (int i = 0; i < ncap; i++) {
    Cap &c = caps[i];
    int inv; int off = findSync(c.d, inv);
    if (off < 0) continue;
    uint8_t d[CAP_LEN];
    for (int b = 0; b < CAP_LEN; b++) d[b] = inv ? ~c.d[b] : c.d[b];
    matches++; invs += inv;
    fes[nfe++] = c.fe;
    char pre[48] = {0}; int p = 0;
    for (int b = off - 40; b < off; b++) pre[p++] = b < 0 ? '.' : '0' + bitAt(d, b);
    char id[24] = "frame cut off";
    int fstart = off + 24;
    if (fstart + 136 <= NB) { uint8_t f[17]; for (int b = 0; b < 17; b++) f[b] = byteAt(d, fstart + b * 8); identify(f, id); }
    char post[80] = {0}; p = 0;
    for (int b = fstart + 136; b < NB && p < 70; b++) { post[p++] = '0' + bitAt(d, b); if (((b - fstart - 136) & 7) == 7) post[p++] = ' '; }
    uint32_t syncT = c.t + off * 4;   // 4 us per bit
    Serial.printf("%3d %7lu %5d %6.0f %3d | %s | %-14s%s | %s\n", i, lastT ? syncT - lastT : 0, (int)c.peak,
                  c.fe * 26000.0 / 16384.0, off, pre, id, inv ? " INV" : "", post);
    lastT = syncT;
    if (matches % 20 == 0) delay(5);
  }
  Serial.printf("\nSUMMARY: %d kept, %d contain B9A199 (%d inverted), %d other\n", ncap, matches, invs, ncap - matches);
  if (nfe) {
    qsort(fes, nfe, sizeof(int), cmpI32);
    Serial.printf("FREQEST (remote vs our CC2500) median %.0f kHz, min %.0f, max %.0f\n",
                  fes[nfe / 2] * 26000.0 / 16384.0, fes[0] * 26000.0 / 16384.0, fes[nfe - 1] * 26000.0 / 16384.0);
  }
}

static void dumpOther() {
  int shown = 0;
  for (int i = 0; i < ncap && shown < 25; i++) {
    int inv; if (findSync(caps[i].d, inv) >= 0) continue;
    Serial.printf("%3d (%d dBm): ", i, caps[i].peak);
    for (int b = 0; b < CAP_LEN; b++) Serial.printf("%02X", caps[i].d[b]);
    Serial.println(); shown++;
  }
  if (!shown) Serial.println("(no non-matching captures)");
}

void setup() {
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_PA, OUTPUT); pinMode(PIN_LNA, OUTPUT);
  digitalWrite(PIN_PA, LOW); digitalWrite(PIN_LNA, LOW);
  pinMode(PIN_GDO0, INPUT); pinMode(PIN_GDO2, INPUT);
  Serial.begin(921600);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  csHigh(); delayMicroseconds(30); digitalWrite(PIN_CSN, LOW); delayMicroseconds(30); csHigh(); delayMicroseconds(50);
  strobe(SRES); delay(2);
  Serial.printf("\nRAW CAPTURE v2 (CC2500 RX, no sync, free-running). PARTNUM 0x%02X (expect 0x80)\n", readStatus(0x30));
  radioConfig();
  setChannel(0);
  Serial.println("Keys: c=capture 4 s | r=RSSI monitor | g/h/k=2402/2448/2479 | ]/[=RSSI gate | m=mode | }/{=CS thr | x=dump other");
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'c' || c == 'C') { captureWindow(); analyze(); }
  else if (c == 'r') rssiMonitor();
  else if (c == 'g') setChannel(0);
  else if (c == 'h') setChannel(1);
  else if (c == 'k') setChannel(2);
  else if (c == ']') { rssiGate += 5; Serial.printf("RSSI gate = %d dBm\n", rssiGate); }
  else if (c == '[') { rssiGate -= 5; Serial.printf("RSSI gate = %d dBm\n", rssiGate); }
  else if (c == '}') { if (csThr < 7) csThr++; applyThr(); }
  else if (c == '{') { if (csThr > 1) csThr--; applyThr(); }
  else if (c == 'm') { syncMode = syncMode ? 0 : 4; applyMode(); Serial.printf("mode = %s\n", syncMode == 4 ? "carrier-sense" : "free-running"); }
  else if (c == 'x') dumpOther();
}
