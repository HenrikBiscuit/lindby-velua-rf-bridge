// RECEIVE BOARD (ESP32 + nRF24L01+PA+LNA) - BUTTON RECORDER   2026-11-19
// Project: velua-nrf24 -> src/main.cpp   (monitor 921600, wiring unchanged)
// (previous reception meter kept in old_sketches/reception_meter_2026-11-19.cpp.txt)
//
// Records whole remote "events" (everything until 300 ms of silence) and prints, per event,
// every DISTINCT frame seen at least twice (garbled packets appear once and are dropped),
// in first-seen order, plus the time sequence of those frames. Works for taps AND holds.
//
//   keys 1-8 = which remote button you are about to record (label goes into the log)
//   g/h/k    = channel 2402 / 2448 / 2479 MHz (default 2402)
//   v        = also print every raw packet (verbose)
//
// The nRF24 matches B9 A1 99 as its address, so payload byte 0 = first byte after the sync.
#include <Arduino.h>
#include <SPI.h>

#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CSN   10
#define PIN_CE     9
#define PIN_IRQ   14

SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);
static uint8_t channel = 2;
static bool verbose = false;
static int button = 0;            // label set by keys 1-8
static uint32_t eventNo = 0;

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
  writeReg(0x00, 0x00);
  writeReg(0x01, 0x00);                  // no auto-ack
  writeReg(0x04, 0x00);                  // no retries
  writeReg(0x06, 0x27);                  // 250 kbps, bit 0 kept set (clone chip)
  writeReg(0x05, channel);
  writeReg(0x1C, 0x00); writeReg(0x1D, 0x00);
  writeReg(0x03, 0x01);                  // 3-byte address
  const uint8_t a0[3] = {0x99, 0xA1, 0xB9};   // on air B9 A1 99
  writeBuf(0x0A, a0, 3);
  writeReg(0x02, 0x01);                  // pipe 0 only
  writeReg(0x11, 32);
  writeReg(0x00, 0x03);                  // PWR_UP + PRIM_RX, CRC off
  delay(5);
  command(0xE2);
  writeReg(0x07, 0x70);
  digitalWrite(PIN_CE, HIGH);
  Serial.printf(">> listening on %u MHz\n", 2400 + channel);
}

// ---- event storage --------------------------------------------------------------------------
static const int KEEP = 24;               // bytes kept per packet (17-byte frame + 7 to check length)
static const int MAXF = 3000;             // ~11 s of packets on one channel
struct Fr { uint32_t ms; uint8_t b[KEEP]; };
static Fr ev[MAXF];
static int nev = 0;
static uint32_t evStart = 0, lastPkt = 0, dropped = 0;

static const int MAXG = 300;
static int gRep[MAXG], gCount[MAXG], gAgree[MAXG];
static uint32_t gFirst[MAXG], gLast[MAXG];
static int16_t frG[MAXF];
static int ng;

static int groupBy(int L) {
  ng = 0;
  for (int i = 0; i < nev; i++) {
    int j;
    for (j = 0; j < ng; j++) if (memcmp(ev[gRep[j]].b, ev[i].b, L) == 0) break;
    if (j == ng) {
      if (ng == MAXG) { frG[i] = -1; continue; }
      gRep[ng] = i; gCount[ng] = 0; gAgree[ng] = KEEP; gFirst[ng] = ev[i].ms; ng++;
    } else {
      int a = 0; while (a < KEEP && ev[gRep[j]].b[a] == ev[i].b[a]) a++;
      if (a < gAgree[j]) gAgree[j] = a;
    }
    gCount[j]++; gLast[j] = ev[i].ms; frG[i] = j;
  }
  int good = 0; for (int j = 0; j < ng; j++) if (gCount[j] >= 2) good++;
  return good;
}

static char letter(int k) { return k < 26 ? 'A' + k : k < 52 ? 'a' + k - 26 : '#'; }

static void analyze() {
  uint32_t dur = ev[nev - 1].ms - ev[0].ms;
  int clean = 0; for (int i = 0; i < nev; i++) if (ev[i].b[0] == 0x0D && ev[i].b[1] == 0xBF && ev[i].b[2] == 0x29) clean++;
  const int lens[3] = {17, 13, 9};
  int L = 17, good = 0;
  for (int k = 0; k < 3; k++) { L = lens[k]; good = groupBy(L); if (good) break; }

  Serial.printf("\n=== EVENT %lu  button %d  ch %u  duration %lu ms  packets %d (start 0DBF29: %d)%s ===\n",
                eventNo, button, channel, dur, nev, clean, dropped ? "  [buffer full, some dropped]" : "");
  if (!good) { Serial.println("  no frame seen twice - too weak? move the remote closer and repeat"); return; }
  if (L != 17) Serial.printf("  NOTE: no 17-byte frame repeated; grouped on first %d bytes\n", L);

  // label repeated groups A, B, C ... in first-seen order
  static int lab[MAXG]; int nl = 0, singles = 0;
  for (int j = 0; j < ng; j++) { if (gCount[j] >= 2) lab[j] = nl++; else { lab[j] = -1; singles++; } }
  Serial.println("  id  first_ms  last_ms  count  frame (17 bytes)                          len");
  for (int j = 0; j < ng; j++) {
    if (lab[j] < 0) continue;
    Serial.printf("  %c  %8lu %8lu %6d  ", letter(lab[j]), gFirst[j] - ev[0].ms, gLast[j] - ev[0].ms, gCount[j]);
    for (int b = 0; b < 17; b++) Serial.printf("%02X%s", ev[gRep[j]].b[b], b == 2 || b == 4 || b == 8 || b == 10 ? " " : "");
    Serial.printf("  %s%d\n", gAgree[j] >= KEEP ? ">=" : "", gAgree[j]);
  }
  Serial.printf("  (%d garbled/one-off packets ignored)\n", singles);

  // time sequence (run-length) of the repeated frames
  Serial.print("  sequence: ");
  int prev = -2, run = 0, runs = 0;
  for (int i = 0; i <= nev && runs < 80; i++) {
    int g = i < nev && frG[i] >= 0 ? lab[frG[i]] : -1;
    if (i < nev && g < 0) continue;
    if (i < nev && g == prev) { run++; continue; }
    if (prev >= 0) { Serial.printf("%c%d ", letter(prev), run); runs++; }
    prev = g; run = 1;
  }
  Serial.println();
}

void setup() {
  pinMode(PIN_CE, OUTPUT); digitalWrite(PIN_CE, LOW);
  pinMode(PIN_IRQ, INPUT_PULLUP);
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  Serial.begin(921600);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  delay(150);
  Serial.println("\nBUTTON RECORDER (nRF24, 250 kbps, sync B9 A1 99)");
  configure();
  Serial.printf("CONFIG 0x%02X RF_SETUP 0x%02X (expect 0x03 / 0x27 or 0x26)\n", readReg(0x00), readReg(0x06));
  Serial.println("Keys: 1-8 = button you are about to record | g/h/k = 2402/2448/2479 | v = verbose");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '8') { button = c - '0'; Serial.printf("\n##### NOW RECORDING BUTTON %d #####\n", button); }
    else if (c == 'g') { channel = 2;  configure(); }
    else if (c == 'h') { channel = 48; configure(); }
    else if (c == 'k') { channel = 79; configure(); }
    else if (c == 'v') { verbose = !verbose; Serial.printf("verbose %s\n", verbose ? "on" : "off"); }
  }

  uint8_t st = readReg(0x07);
  if (digitalRead(PIN_IRQ) == LOW || (st & 0x40)) {
    uint8_t b[32];
    readPayload(b);
    writeReg(0x07, 0x40);
    uint32_t now = millis();
    if (nev == 0) { evStart = now; dropped = 0; }
    if (nev < MAXF) { ev[nev].ms = now; memcpy(ev[nev].b, b, KEEP); nev++; } else dropped++;
    lastPkt = now;
    if (verbose) {
      Serial.printf("  P %lu ", now);
      for (int i = 0; i < 32; i++) Serial.printf("%02X", b[i]);
      Serial.println();
    }
  }

  if (nev && millis() - lastPkt > 300) {   // event over
    eventNo++;
    analyze();
    nev = 0;
  }
}
