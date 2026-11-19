// TRANSMIT BOARD (ESP32-S3 + CC2500PA) - UNSEEN-FRAME test, 250 kbps      2026-11-19 v3
// Project: nrf24-antenna-test -> src/main.cpp   (monitor 115200)
//
// v2 proved our replay is bit-perfect on the receive board, but the lamp ignored a frame
// it had already received live from the remote -> rolling/anti-replay counter suspected.
//
// v3 is the decisive test: replay 3 On/off taps the lamp has NEVER received. They were
// recorded (log 144336) while the lamp was switched OFF at the wall, so its counter never
// advanced to them. Each tap = press frame (~100 ms) + release frame (~1.6 s), full length,
// consensus bytes from many clean copies:
//   tap1 press  9A 20 D7 C7 CB 14 A1 B6 CE 8A F3 62 E7 70 / release 8B 21 DF 5E 4B 9C A1 B6 DF 9A C2 E4 91 03
//   tap2 press  8B 34 5F C7 CB 14 A1 B6 DF 9A 4D DD 6B 2D / release B9 22 57 5E 4B 9C A1 B6 ED 99 1E A7 3E AC
//   tap3 press  B9 23 D7 C7 CB 14 A1 B6 ED 99 91 BE C4 82 / release A8 32 DF 5E 4B 9C A1 B6 FC 89 A0 18 B2 F1
//
// The 3 taps are sent in turn, one every ~10 s. GREEN blinks BEFORE each tap tell you which
// one (1/2/3) is next, so if the lamp reacts you know which counter it accepted.
//   green x1/2/3 = which tap is next   red = sending   slow blue = pause
// Power: PATABLE 0x46 (about -20 dBm at the chip), unchanged.
// The old sketch is kept in old_sketches/transmit_replay_13byte_2026-11-19.cpp.txt.
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

// Each packet, as a bit stream: 7 filler bits + 32 preamble bits (1100 x 8) + 160 data
// bits (B9 A1 99 + the 17 bytes above) + trailer 1010 + 5 zero bits = 208 bits = 26 bytes.
// Same lead-in as the old sketch (checked: decoding the old arrays gives exactly their data).
static const uint8_t PKT_LEN = 26;
// [tap][0=press,1=release][26 bytes]
static const uint8_t TAPS[3][2][PKT_LEN] = {
  { {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x34, 0x41, 0xAF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0x9D, 0x15, 0xE6, 0xC5, 0xCE, 0xE1, 0x40},
    {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x16, 0x43, 0xBE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xBF, 0x35, 0x85, 0xC9, 0x22, 0x07, 0x40} },  // tap1
  { {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x16, 0x68, 0xBF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xBF, 0x34, 0x9B, 0xBA, 0xD6, 0x5B, 0x40},
    {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x72, 0x44, 0xAE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xDB, 0x32, 0x3D, 0x4E, 0x7D, 0x59, 0x40} },  // tap2
  { {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x72, 0x47, 0xAF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xDB, 0x33, 0x23, 0x7D, 0x89, 0x05, 0x40},
    {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x50, 0x65, 0xBE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xF9, 0x13, 0x40, 0x31, 0x65, 0xE3, 0x40} },  // tap3
};
static const uint32_t PRESS_MS = 100, RELEASE_MS = 1600;

static const float CH_MHZ[] = {2448.00, 2478.95, 2402.00};
static const int   PKTS_PER_CH = 3;
static const int   GAP_US = 650;            // remote: ~1.2 ms packet-to-packet

SPISettings spiCfg(4000000, MSBFIRST, SPI_MODE0);

static void csLow() {
  digitalWrite(PIN_CSN, LOW);
  uint32_t t = micros();
  while (digitalRead(PIN_MISO) && micros() - t < 1000) {}
}
static void csHigh() { digitalWrite(PIN_CSN, HIGH); }
static void strobe(uint8_t s) {
  SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(s); csHigh(); SPI.endTransaction();
}
static void writeReg(uint8_t a, uint8_t v) {
  SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a); SPI.transfer(v); csHigh(); SPI.endTransaction();
}
static uint8_t readStatus(uint8_t a) {
  SPI.beginTransaction(spiCfg); csLow(); SPI.transfer(a | 0xC0); uint8_t v = SPI.transfer(0); csHigh(); SPI.endTransaction();
  return v;
}
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
  writeReg(PKTLEN,   PKT_LEN);
  writeReg(PKTCTRL1, 0x00);
  writeReg(PKTCTRL0, 0x00);   // fixed length, no CRC, no whitening, FIFO mode
  writeReg(ADDR,     0x00);
  writeReg(CHANNR,   0x00);
  writeReg(FSCTRL1,  0x10);
  writeReg(FSCTRL0,  0x00);
  writeReg(MDMCFG4,  0x0D);   // DRATE_E 13
  writeReg(MDMCFG3,  0x3B);   // -> 249.9 kbps
  writeReg(MDMCFG2,  0x00);   // 2-FSK, chip adds no preamble/sync (ours is in PKT)
  writeReg(MDMCFG1,  0x22);
  writeReg(MDMCFG0,  0xF8);
  writeReg(DEVIATN,  0x73);   // 279 kHz
  writeReg(MCSM1,    0x32);   // after TX -> FSTXON (synthesizer stays on)
  writeReg(MCSM0,    0x08);   // no autocal; we calibrate per channel
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
  writeReg(PATABLE,  0x46);   // about -20 dBm at the chip
}

static void setChannel(float mhz) {
  strobe(SIDLE);
  uint32_t t = micros();
  while (state() != 0x01 && micros() - t < 2000) {}
  uint32_t w = (uint32_t)((double)mhz * 1e6 * 65536.0 / 26e6 + 0.5);
  writeReg(FREQ2, (w >> 16) & 0xFF);
  writeReg(FREQ1, (w >> 8) & 0xFF);
  writeReg(FREQ0, w & 0xFF);
  strobe(SCAL);
  t = micros();
  while (state() != 0x01 && micros() - t < 2000) {}   // calibration done -> IDLE
  strobe(SFSTXON);
  t = micros();
  while (state() != 0x12 && micros() - t < 2000) {}   // FSTXON
}

static void sendPacket(const uint8_t *pkt) {
  writeFifo(pkt, PKT_LEN);
  strobe(STX);
  uint32_t t = micros();
  while (state() != 0x13 && micros() - t < 1000) {}    // wait until really in TX
  while (state() == 0x13 && micros() - t < 3000) {}    // wait until finished
  if (state() == 0x16) { strobe(SIDLE); strobe(SFTX); strobe(SFSTXON); }  // underflow recovery
}

static void ampOff() { digitalWrite(PIN_PA, LOW); digitalWrite(PIN_LNA, LOW); }
static void ampTx()  { digitalWrite(PIN_PA, HIGH); digitalWrite(PIN_LNA, LOW); }   // "pin A", measured 2026-11-19

static void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_BUILTIN, r, g, b); }

static void sendFor(const uint8_t *pkt, uint32_t ms, int &ch) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    setChannel(CH_MHZ[ch]);
    for (int i = 0; i < PKTS_PER_CH; i++) { sendPacket(pkt); delayMicroseconds(GAP_US); }
    ch = (ch + 1) % 3;
  }
}

static void tap(int t) {
  ampTx();
  led(40, 0, 0);
  int ch = 0;
  sendFor(TAPS[t][0], PRESS_MS, ch);
  sendFor(TAPS[t][1], RELEASE_MS, ch);
  strobe(SIDLE);
  ampOff();
  led(0, 0, 0);
}

void setup() {
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_PA, OUTPUT); pinMode(PIN_LNA, OUTPUT); ampOff();
  pinMode(PIN_GDO0, INPUT); pinMode(PIN_GDO2, INPUT);
  Serial.begin(115200);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  radioReset();
  Serial.printf("\nTRANSMIT BOARD v3: unseen-frame test, 3 fresh On/off taps, 250k. PARTNUM 0x%02X (expect 0x80)\n",
                readStatus(0x30));
  radioConfig();
  for (int i = 0; i < 5; i++) { led(0, 0, 40); delay(500); led(0, 0, 0); delay(500); }
}

void loop() {
  static int t = 0;
  // green blinks = which fresh tap (1-3) is about to be sent
  for (int i = 0; i <= t; i++) { led(0, 40, 0); delay(250); led(0, 0, 0); delay(250); }
  delay(700);
  Serial.printf("Unseen tap %d\n", t + 1);
  tap(t);
  t = (t + 1) % 3;
  for (int i = 0; i < 6; i++) { led(0, 0, 15); delay(100); led(0, 0, 0); delay(900); }
}