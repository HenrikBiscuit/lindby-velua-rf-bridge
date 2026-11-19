// A7105 RECEIVER / SCANNER for the Lindby Velua remote                     2026-11-19 v1
// Board: spare ESP32-S3-DevKitC-1.  Module: XL7105-SY-B (AMICCOM A7105).  Monitor 115200.
//
// WHY THIS CHIP
// The remote's unmarked SOP16 + 16 MHz crystal is almost certainly an A7105-class radio.
// Our nRF24/CC2500 could copy the on-air BITS perfectly, but the lamp never reacted - most
// likely because those chips can't reproduce the remote's real preamble / ID-sync / CRC /
// data-whitening, so the lamp's own demodulator never locked. The A7105 does all of that in
// hardware. Mapping our capture (B9 A1 99 0D BF 29 ...) onto the A7105 packet format
//   [4-byte preamble][4-byte ID code][payload (+whitening)][2-byte CRC]
// the 4-byte ID code is very likely B9 A1 99 0D and the rest is payload. If the remote uses
// A7105 whitening, our old captures were the *whitened* payload (that's why the counter and
// tail looked random) - this receiver will de-whiten it in hardware and show the real bytes.
//
// GOAL OF v1: (1) prove the ESP talks to the A7105 (SPI round-trip + calibration pass) -
// this also confirms the unmarked-pad wiring - and (2) try to receive the remote, printing
// any packet with its CRC/FEC flags. Reception parameters (ID window, whitening, CRC,
// channel) are tunable live over the serial monitor so we can hunt for a lock.
//
// Datasheet facts used (AMICCOM A7105 v1.4):
//   SPI addr byte: bit7 0=reg/1=strobe, bit6 0=write/1=read, bits5-0 = address.
//   Strobes: SLEEP 0x80 IDLE 0x90 STANDBY 0xA0 PLL 0xB0 RX 0xC0 TX 0xD0 TXFIFO-rst 0xE0 RXFIFO-rst 0xF0
//   Mode reg 0x00 read: bit6 FECF (1=FEC error), bit5 CRCF (1=CRC error).
//   Data rate = FSYCK/32/(SDR+1); reg 0x0E. Channel number = reg 0x0F (CHN).
//   Code I 0x1F: bit5 WHTS(whiten) bit4 FECS(fec) bit3 CRCS(crc) bit2 IDL(1=4-byte ID) bits1:0 PML(11=4-byte preamble).
//   Calibration: standby -> IF filter bank (0x02 bit0 FBC), then per-channel VCO (0x02 bit1 VBC, bit2 VCC); bits auto-clear.
#include <Arduino.h>

// ---- Wiring (ESP32-S3 GPIO -> A7105 module). Avoids PSRAM 35-37, strapping 0/3/45/46,
//      USB 19/20, RGB 38/48. 3-wire SPI: SDIO is bidirectional (bit-banged). ----
#define PIN_SCS   10   // A7105 SCS  (chip select)
#define PIN_SCK   12   // A7105 SCK  (clock)
#define PIN_SDIO  11   // A7105 SDIO (bidirectional data)
#define PIN_GIO1  13   // A7105 GIO1 (status out; configured as "WtR" - low when a packet is received)
// VDD -> 3V3 (NEVER 5V), GND -> GND. GIO2 left unconnected.

// ---------------- low-level 3-wire SPI (bit-banged, MSB first) ----------------
static inline void sckPulseWrite(uint8_t bit) {
  digitalWrite(PIN_SDIO, bit);
  digitalWrite(PIN_SCK, HIGH);        // A7105 latches on rising edge
  digitalWrite(PIN_SCK, LOW);
}
static void spiWrite(uint8_t v) {
  pinMode(PIN_SDIO, OUTPUT);
  for (int i = 7; i >= 0; i--) sckPulseWrite((v >> i) & 1);
}
static uint8_t spiRead() {
  pinMode(PIN_SDIO, INPUT);
  uint8_t v = 0;
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_SCK, HIGH);      // MCU samples while SCK high
    v = (v << 1) | (digitalRead(PIN_SDIO) & 1);
    digitalWrite(PIN_SCK, LOW);
  }
  return v;
}
static inline void csLow()  { digitalWrite(PIN_SCS, LOW); }
static inline void csHigh() { digitalWrite(PIN_SCS, HIGH); }

static void strobe(uint8_t cmd) { csLow(); spiWrite(cmd); csHigh(); }
static void writeReg(uint8_t a, uint8_t v) { csLow(); spiWrite(a & 0x3F); spiWrite(v); csHigh(); }
static uint8_t readReg(uint8_t a) { csLow(); spiWrite(0x40 | (a & 0x3F)); uint8_t v = spiRead(); csHigh(); return v; }
static void writeID(const uint8_t id[4]) {               // reg 0x06 auto-sequences 4 ID bytes
  csLow(); spiWrite(0x06); for (int i = 0; i < 4; i++) spiWrite(id[i]); csHigh();
}
static void readFIFO(uint8_t *b, int n) {
  strobe(0xF0);                                          // reset RX FIFO read pointer
  csLow(); spiWrite(0x40 | 0x05); pinMode(PIN_SDIO, INPUT);
  for (int i = 0; i < n; i++) b[i] = spiRead();
  csHigh();
}

// strobe command bytes
enum { S_SLEEP=0x80, S_IDLE=0x90, S_STANDBY=0xA0, S_PLL=0xB0, S_RX=0xC0, S_TX=0xD0, S_TXRST=0xE0, S_RXRST=0xF0 };

// ---------------- tunable reception parameters ----------------
// ID-code candidates: slide the 4-byte window across the constant header B9 A1 99 0D BF 29.
static const uint8_t ID_CANDS[][4] = {
  {0xB9, 0xA1, 0x99, 0x0D},   // 0: most likely (header start)
  {0xA1, 0x99, 0x0D, 0xBF},   // 1
  {0x99, 0x0D, 0xBF, 0x29},   // 2
  {0x0D, 0xBF, 0x29, 0x00},   // 3
};
static int      idSel    = 0;
static bool     useWhiten= false;   // off for the raw hunt (turn on once we get packets)
static bool     useCRC   = false;   // off so ANY ID-synced packet prints, CRC right or not
static bool     useFEC   = false;
static uint8_t  dataRate = 0x01;    // 0x0E: SDR -> FSYCK/32/(SDR+1). 0=500k 1=250k 2=167k 3=125k (FSYCK=16MHz)
static uint8_t  rxReg18  = 0x62;    // RX register 0x18; bit1 BWS = IF bandwidth select (tunable with 'b')
static uint8_t  pml      = 3;       // preamble length: 0=1B 1=2B 2=3B 3=4B (Code I PML bits), tunable with 'p'
static const uint8_t CHANS[] = {2, 48, 79};   // 2402 / 2448 / 2479 MHz (confirmed by RSSI scan: remote at 2402)
static int      chSel    = 0;       // lock ch2 (2402 MHz) - the confirmed-strong channel
static const int PAYLOAD = 24;      // bytes to pull from RX FIFO per packet (header+payload+crc)

// Code register I (0x1F) from the live flags
static uint8_t codeI() {
  // bit5 WHTS, bit4 FECS, bit3 CRCS, bit2 IDL(=1 -> 4-byte ID), bits1:0 PML(=11 -> 4-byte preamble)
  return (useWhiten ? 0x20 : 0) | (useFEC ? 0x10 : 0) | (useCRC ? 0x08 : 0) | 0x04 | (pml & 0x03);
}

// ---------------- init + calibration ----------------
// Base register table: the de-facto A7105 2.4 GHz reference init (GFSK, FIFO mode, DBL on,
// base freq so CHN reg ~= 2400+CHN MHz). 0xFF = leave at reset. We override data rate (0x0E),
// Code I (0x1F) and the ID (0x06) below. Analog/PLL/RX-gain values may still need tuning to
// lock onto the remote - that's the next iteration.
static const uint8_t INIT[0x33] = {
  0xFF, 0x42, 0x00, 0x14, 0x00, 0xFF, 0xFF, 0x00,  // 00-07
  0x00, 0x00, 0x00, 0x01, 0x21, 0x05, 0x01, 0x50,  // 08-0F  (0x0E data rate overridden in code)
  0x9E, 0x4B, 0x00, 0x02, 0x16, 0x2B, 0x12, 0x4F,  // 10-17
  0x62, 0x80, 0x80, 0x00, 0x0A, 0x32, 0xC3, 0x2F,  // 18-1F  (0x1F Code I overridden in code)
  0x13, 0xC3, 0x00, 0xFF, 0x00, 0x00, 0x3B, 0x00,  // 20-27
  0x17, 0x47, 0x80, 0x03, 0x01, 0x45, 0x18, 0x00,  // 28-2F
  0x01, 0x0F, 0xFF                                  // 30-32
};

static bool calibrate() {
  // IF filter bank calibration in standby
  strobe(S_STANDBY);
  writeReg(0x02, 0x01);                 // FBC = 1
  uint32_t t = millis();
  while ((readReg(0x02) & 0x01) && millis() - t < 50) {}
  uint8_t ifcal = readReg(0x22);        // bit4 FBCF: 1 = fail
  // VCO bank + current calibration in PLL mode, at a mid channel
  writeReg(0x0F, CHANS[1]);
  strobe(S_PLL);
  writeReg(0x02, 0x06);                 // VBC(bit1)+VCC(bit2) = 1
  t = millis();
  while ((readReg(0x02) & 0x06) && millis() - t < 50) {}
  uint8_t vco = readReg(0x25);          // bit3 VBCF: 1 = fail
  strobe(S_STANDBY);
  Serial.printf("  calibration: IF 0x22=0x%02X (FBCF %s), VCO 0x25=0x%02X (VBCF %s)\n",
                ifcal, (ifcal & 0x10) ? "FAIL" : "ok", vco, (vco & 0x08) ? "FAIL" : "ok");
  return !(ifcal & 0x10) && !(vco & 0x08);
}

static void applyConfig() {
  writeReg(0x0E, dataRate);
  writeReg(0x18, rxReg18);
  writeReg(0x1F, codeI());
  writeID(ID_CANDS[idSel]);
}
static uint16_t rateKbps() { return 500 / (dataRate + 1); }  // FSYCK=16MHz assumption

static void radioInit() {
  strobe(S_RXRST); strobe(S_TXRST);
  writeReg(0x00, 0x00);                 // software reset
  delay(1);
  for (uint8_t a = 0; a < 0x33; a++) if (INIT[a] != 0xFF) writeReg(a, INIT[a]);
  applyConfig();
  calibrate();
}

static void banner() {
  Serial.printf("\n>> A7105 ID=%02X %02X %02X %02X  whiten=%d crc=%d fec=%d  rate0x%02X(~%ukbps)  rx18=0x%02X  chan=%s\n",
                ID_CANDS[idSel][0], ID_CANDS[idSel][1], ID_CANDS[idSel][2], ID_CANDS[idSel][3],
                useWhiten, useCRC, useFEC, dataRate, rateKbps(), rxReg18,
                chSel < 0 ? "sweep 2/48/79" : String(CHANS[chSel]).c_str());
  Serial.println("   keys: i=ID  w=whiten  c=crc  f=fec  g/h/k=chan 2/48/79  s=sweep");
  Serial.printf("         d=data rate(500/250/167/125k)  b=IF bandwidth  p=preamble len(%dB)  z=RSSI scan\n", pml + 1);
}

void setup() {
  pinMode(PIN_SCS, OUTPUT);  digitalWrite(PIN_SCS, HIGH);
  pinMode(PIN_SCK, OUTPUT);  digitalWrite(PIN_SCK, LOW);
  pinMode(PIN_SDIO, OUTPUT);
  pinMode(PIN_GIO1, INPUT);
  Serial.begin(115200);
  delay(1500);

  // SPI round-trip test: write an ID, read it back. Proves wiring + SPI before anything else.
  strobe(S_RXRST);
  writeReg(0x00, 0x00); delay(1);
  const uint8_t probe[4] = {0xB9, 0xA1, 0x99, 0x0D};
  writeID(probe);
  uint8_t rb[4]; csLow(); spiWrite(0x40 | 0x06); for (int i=0;i<4;i++) rb[i]=spiRead(); csHigh();
  bool spiOk = (rb[0]==0xB9 && rb[1]==0xA1 && rb[2]==0x99 && rb[3]==0x0D);
  Serial.printf("\nA7105 SPI round-trip: wrote B9 A1 99 0D, read %02X %02X %02X %02X -> %s\n",
                rb[0], rb[1], rb[2], rb[3], spiOk ? "OK (wiring good)" : "FAIL (check wiring!)");
  if (!spiOk) Serial.println("   If FAIL: verify SCS/SCK/SDIO and that VDD=3.3V. No point receiving until this passes.");

  radioInit();
  banner();
}

static void startRX(uint8_t ch) {
  strobe(S_STANDBY);
  writeReg(0x0F, ch);        // channel
  strobe(S_PLL);
  strobe(S_RXRST);
  strobe(S_RX);
}

// ---- RSSI energy scan: finds WHERE the remote transmits, independent of data rate /
// deviation / ID. Per datasheet 17.1 (background power): enable ARSSI, RX, wait >=140us,
// read ADC[7:0] (reg 0x1D). Higher value = stronger signal. Sweeps CHN 0..99 (~2400..2499
// MHz, so CHN == MHz-2400 if the base is as assumed). Hold On/off DOWN during the scan. ----
static bool rssiMode = false;
static bool autoSweep = false;        // auto-cycle preamble x ID so the user just holds the button
static uint32_t sweepT = 0;
static uint8_t rssiBase[100];   // noise floor per channel, captured on entering scan mode
static bool haveBase = false;

static uint8_t measureRSSI(uint8_t ch) {
  strobe(S_STANDBY);
  writeReg(0x0F, ch);
  strobe(S_PLL);
  strobe(S_RX);
  delayMicroseconds(200);       // >=140us for the 8x average
  uint8_t r = readReg(0x1D);    // ADC[7:0] = RSSI
  strobe(S_STANDBY);
  return r;
}

static void rssiScan() {
  uint8_t v[100];
  for (int ch = 0; ch < 100; ch++) v[ch] = measureRSSI(ch);
  if (!haveBase) { memcpy(rssiBase, v, 100); haveBase = true;
    Serial.println("RSSI baseline captured (button UP). Now HOLD On/off and watch for peaks.");
    return; }
  // print channels that rise clearly above their own baseline
  Serial.print("RSSI peaks: ");
  bool any = false;
  for (int ch = 0; ch < 100; ch++) {
    int d = (int)v[ch] - (int)rssiBase[ch];
    if (d >= 12) { Serial.printf("ch%d(+%d,%d MHz) ", ch, d, 2400 + ch); any = true; }
  }
  Serial.println(any ? "" : "(none above +12)");
}

void loop() {
  static uint32_t winStart = millis(), pk = 0, good = 0;
  static int sweepIdx = 0;

  while (Serial.available()) {
    char c = Serial.read();
    bool reinit = true;
    if      (c == 'i') idSel = (idSel + 1) % 4;
    else if (c == 'w') useWhiten = !useWhiten;
    else if (c == 'c') useCRC = !useCRC;
    else if (c == 'f') useFEC = !useFEC;
    else if (c == 'g') chSel = 0;
    else if (c == 'h') chSel = 1;
    else if (c == 'k') chSel = 2;
    else if (c == 's') chSel = -1;
    else if (c == 'd') dataRate = (dataRate + 1) & 0x03;   // cycle 500/250/167/125 kbps
    else if (c == 'b') rxReg18 ^= 0x02;                    // flip BWS (IF bandwidth)
    else if (c == 'p') pml = (pml + 1) & 0x03;             // cycle preamble length 1..4 bytes
    else if (c == 'a') { autoSweep = !autoSweep; sweepT = millis();
      Serial.println(autoSweep ? ">> AUTO-SWEEP preamble x ID at 250k/ch2 - HOLD On/off ~40s"
                               : ">> auto-sweep off"); reinit = false; }
    else if (c == 'r') {}
    else if (c == 'z') { rssiMode = !rssiMode; haveBase = false;
      if (rssiMode) { writeReg(0x01, readReg(0x01) | 0x40);   // ARSSI = 1
        Serial.println(">> RSSI scan. Press z again with button UP to set baseline, then HOLD On/off."); }
      else { radioInit(); banner(); }
      reinit = false; }
    else reinit = false;
    if (reinit) { applyConfig(); banner(); }
  }

  if (rssiMode) { rssiScan(); delay(150); return; }

  if (autoSweep && millis() - sweepT >= 2500) {           // advance preamble, then ID
    pml = (pml + 1) & 0x03;
    if (pml == 0) idSel = (idSel + 1) % 4;
    dataRate = 0x01; chSel = 0;                            // keep 250k / ch2
    applyConfig(); banner(); sweepT = millis();
  }

  uint8_t ch = (chSel < 0) ? CHANS[sweepIdx] : CHANS[chSel];
  startRX(ch);

  // Detect a received packet by a CHANGE on GIO1 (WtR) from its waiting level - works whether
  // WtR asserts high or low, so this no longer depends on guessing the polarity.
  delayMicroseconds(60);                 // let RX settle into the waiting state
  int idle = digitalRead(PIN_GIO1);
  uint32_t t = micros();
  bool got = false;
  while (micros() - t < 7000) { if (digitalRead(PIN_GIO1) != idle) { got = true; break; } }

  if (got) {
    uint8_t mode = readReg(0x00);
    uint8_t b[PAYLOAD];
    readFIFO(b, PAYLOAD);
    pk++;
    bool crcErr = mode & 0x20, fecErr = mode & 0x40;
    if (!crcErr) good++;
    Serial.printf("P %lu ch%u crc%s fec%s ", millis(), ch, crcErr ? "ERR" : "OK", fecErr ? "ERR" : "ok");
    for (int i = 0; i < PAYLOAD; i++) Serial.printf("%02X", b[i]);
    Serial.println();
  }

  if (chSel < 0) sweepIdx = (sweepIdx + 1) % 3;

  if (millis() - winStart >= 2000) {
    // heartbeat so silence is visibly "listening", not a hang; gio1 shows the WtR pin level
    Serial.printf("S %lu listening ch%u ~%ukbps rx18=0x%02X gio1=%d  packets=%lu\n",
                  millis(), ch, rateKbps(), rxReg18, digitalRead(PIN_GIO1), pk);
    winStart = millis(); pk = good = 0;
  }
}
