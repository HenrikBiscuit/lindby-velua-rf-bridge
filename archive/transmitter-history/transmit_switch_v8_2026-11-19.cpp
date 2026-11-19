// TRANSMIT BOARD (ESP32-S3 + CC2500PA) - On/off switch v8 (MCSM1 TXOFF fix + tap stats)   2026-11-19
// Project: nrf24-antenna-test -> src/main.cpp   (monitor 115200)
//
// BREAKTHROUGH: the On/off "rolling code" is just an 8-state counter that the remote cycles
// through in a FIXED order, wrapping around (confirmed over 30 taps, log 180635):
//     B922 -> A832 -> A911 -> B801 -> 8A02 -> 9B12 -> 9A31 -> 8B21 -> (repeat)
// Plain replay failed because the lamp expects the NEXT state after the one it last accepted.
// We now hold all 8 valid frames, so we can give it the next one.
//
// This sketch walks the 8 states IN ORDER, one every ~4 s, looping forever. Whatever position
// the lamp is at, within one lap we hit its "expected next" and it should start toggling.
// Serial prints which state is being sent so you can match a lamp reaction to a state.
//   red = sending   blue blink = gap
// NOTE: sending RELEASE frames only for now (all captured rock-solid). If the lamp needs the
// Power: PATABLE 0x46 - KNOWN-GOOD PA drive that toggled the lamp; raising it overdrives the CC2591 PA.
// The previous sketch is kept in old_sketches/transmit_3taps_v3_2026-11-19.cpp.txt.
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>     // persist the sequence pointer across reboots

#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_CSN   10
#define PIN_GDO0   4
#define PIN_GDO2   5
#define PIN_PA     6
#define PIN_LNA    7
#define PIN_BTN    0     // onboard BOOT button = portable "toggle" trigger (no PC needed)

// The remote's On/off counter cycles through 8 FIXED states in THIS order, wrapping around
// (confirmed over 30 taps, log 180635). Each row is one state's RELEASE frame, packed on-air
// (7 filler + 1100x8 preamble + B9 A1 99 + 17-byte frame + 1010 trailer = 26 bytes).
// We send the release frames only (all captured rock-solid); if the lamp needs the press
// burst too we add it next. The lamp expects the NEXT state after the one it last accepted,
// so walking these in order should, once aligned, be accepted on each send.
static const uint8_t PKT_LEN = 26;
static const int NSTATES = 8;
// PRESS frame per state (button-down; for On/off this is likely what toggles the lamp).
static const uint8_t PRESSES[NSTATES][PKT_LEN] = {
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x16, 0x60, 0xBF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xBF, 0x34, 0x9B, 0xBA, 0xD6, 0x5B, 0x40},  // 0 press 8B30
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x72, 0x67, 0xAF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xDB, 0x33, 0x23, 0x3D, 0x89, 0x05, 0x40},  // 1 press B933
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x50, 0x46, 0xBF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xF9, 0x12, 0x5E, 0x42, 0x91, 0xBF, 0x40},  // 2 press A823
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x52, 0x01, 0xAF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xFB, 0x54, 0x5D, 0xC7, 0x33, 0x35, 0x40},  // 3 press A900
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x70, 0x20, 0xBF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xD9, 0x75, 0x20, 0xB8, 0x2B, 0x8F, 0x40},  // 4 press B810
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x14, 0x27, 0xAF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0xBD, 0x72, 0x98, 0x3F, 0x74, 0xD1, 0x40},  // 5 press 8A13
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x36, 0x06, 0xBF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0x9F, 0x53, 0xE5, 0x40, 0x6C, 0x6B, 0x40},  // 6 press 9B03
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x34, 0x41, 0xAF, 0x8F, 0x96, 0x29, 0x43, 0x6D, 0x9D, 0x15, 0xE6, 0xC5, 0xCE, 0xE1, 0x40},  // 7 press 9A20
};
// RELEASE frame per state (button-up), same 8-state cycle.
static const uint8_t STATES[NSTATES][PKT_LEN] = {
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x72, 0x44, 0xAE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xDB, 0x32, 0x3D, 0x4E, 0x7D, 0x59, 0x40},  // 0 cnt B922
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x50, 0x65, 0xBE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xF9, 0x13, 0x40, 0x31, 0x65, 0xE3, 0x40},  // 1 cnt A832
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x52, 0x22, 0xAE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xFB, 0x55, 0x43, 0xB4, 0xC7, 0x69, 0x40},  // 2 cnt A911
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x70, 0x03, 0xBE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xD9, 0x74, 0x3E, 0xCB, 0xDF, 0xD3, 0x40},  // 3 cnt B801
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x14, 0x04, 0xAE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xBD, 0x73, 0x86, 0x4C, 0x80, 0x8D, 0x40},  // 4 cnt 8A02
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x36, 0x25, 0xBE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0x9F, 0x52, 0xFB, 0x33, 0x98, 0x37, 0x40},  // 5 cnt 9B12
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x34, 0x62, 0xAE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0x9D, 0x14, 0xF8, 0xB6, 0x3A, 0xBD, 0x40},  // 6 cnt 9A31
  {0x99, 0x99, 0x99, 0x99, 0x99, 0x73, 0x43, 0x32, 0x1B, 0x7E, 0x53, 0x16, 0x43, 0xBE, 0xBC, 0x97, 0x39, 0x43, 0x6D, 0xBF, 0x35, 0x85, 0xC9, 0x22, 0x07, 0x40},  // 7 cnt 8B21
};
static const char *CNT[NSTATES] = {"B922","A832","A911","B801","8A02","9B12","9A31","8B21"};
static const uint32_t PRESS_MS = 100, RELEASE_MS = 1600;   // like the real remote's tap

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
  // v8 FIX: was 0x32, which sets TXOFF_MODE=TX (bits[1:0]=10): the chip NEVER left TX after a
  // packet, so between packets it kept keying the carrier / underflowing until our 3 ms timeout.
  // 0x31 = TXOFF_MODE FSTXON: packet ends -> carrier off, synthesizer stays locked. Clean bursts.
  writeReg(MCSM1,    0x31);
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
  writeReg(PATABLE,  0x46);   // KNOWN-GOOD drive for the CC2591 PA (toggled the lamp). Do NOT raise - max overdrives the PA and the lamp stops decoding.
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

// per-tap diagnostics (printed after each tap)
static uint32_t stPkts, stUnder, stTimeout, stTxUsSum, stFirstUs, stLastUs;

static void sendPacket(const uint8_t *pkt) {
  writeFifo(pkt, PKT_LEN);
  strobe(STX);
  uint32_t t = micros();
  if (stPkts == 0) stFirstUs = t;
  stLastUs = t;
  while (state() != 0x13 && micros() - t < 1000) {}    // wait until really in TX
  while (state() == 0x13 && micros() - t < 3000) {}    // wait until finished
  uint32_t dt = micros() - t;
  stTxUsSum += dt;
  if (dt >= 3000) stTimeout++;                         // never left TX = bad
  stPkts++;
  if (state() == 0x16) { stUnder++; strobe(SIDLE); strobe(SFTX); strobe(SFSTXON); }  // underflow recovery
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

static void sendState(int s) {
  ampTx();
  led(40, 0, 0);
  int ch = 0;
  stPkts = stUnder = stTimeout = stTxUsSum = 0;
  sendFor(PRESSES[s], PRESS_MS, ch);     // button-down burst (~100 ms), like the remote
  sendFor(STATES[s],  RELEASE_MS, ch);   // button-up burst (~1.6 s)
  strobe(SIDLE);
  ampOff();
  led(0, 0, 0);
  // Expect: TX time ~850 us/pkt (26 B @ 250k), 0 timeouts, 0 underflows, period ~1.6-1.8 ms.
  Serial.printf("  tap stats: %lu pkts, avg TX %lu us/pkt, period %lu us, timeouts %lu, underflows %lu\n",
                stPkts, stPkts ? stTxUsSum / stPkts : 0,
                stPkts > 1 ? (stLastUs - stFirstUs) / (stPkts - 1) : 0, stTimeout, stUnder);
}

// ---------------- deterministic On/off switch (v7) ----------------
// Press 't' to toggle the lamp. We track the lamp's position in the 8-state cycle and send the
// state it expects next, REPEATS times (repeats of the same state are safe: the lamp toggles
// once and ignores the rest - they only guard against a dropped tap). Then we advance. It
// self-syncs: if our pointer is off, early toggles may miss, then it locks on.
// Reliability depends on the RF link - keep the board close to the lamp. Single taps landed
// ~45% in testing; REPEATS taps per toggle makes a command land ~1-(0.55^R).
//   t / s  : toggle the lamp          + / - : more / fewer repeats
//   0-7    : set pointer to a state   r     : reset pointer to 0     h : help
Preferences prefs;
static uint8_t ptr = 0;
// IMPORTANT: each tap (press+release) is ONE separate toggle to the lamp - the counter does
// NOT gate it. So REPEATS is literally how many times the lamp flips per command. Keep it at 1
// for a normal on/off. (It is NOT a reliability knob - reliability comes from RF range/power.
// More repeats just makes the net result an even/odd lottery when some taps drop.)
static int REPEATS = 1;
// CC2500 PATABLE drive into the CC2591 PA, low -> high. Per the module datasheet, TOO MUCH
// power at short range (<0.5 m) overloads the receiver and BLOCKS communication - so close to
// the lamp use LOW power, far away use high. ']' steps up, '[' steps down. Dial to the lowest
// level that still toggles at your mounting distance.
static const uint8_t PWR[] = {0x00, 0x1D, 0x46, 0x60, 0x84, 0xA0, 0xC0, 0xE0, 0xFE};
static const int NPWR = sizeof(PWR);
static int pwr = 2;

static void savePtr() { prefs.putUChar("ptr", ptr); }
static void applyPwr() { writeReg(PATABLE, PWR[pwr]); prefs.putInt("pwr2", pwr); Serial.printf("power = PATABLE 0x%02X (level %d/%d) %s\n", PWR[pwr], pwr + 1, NPWR, pwr <= 2 ? "[low - good for CLOSE range]" : pwr >= 6 ? "[high - for FAR/through walls]" : ""); }
static void saveRep() { prefs.putInt("rep", REPEATS); }

static void toggleLamp() {
  Serial.printf("TOGGLE -> %d flip(s), state %d (cnt %s), PATABLE 0x%02X\n", REPEATS, ptr, CNT[ptr], PWR[pwr]);
  for (int r = 0; r < REPEATS; r++) sendState(ptr);
  ptr = (ptr + 1) % NSTATES;      // advance so consecutive presses use a fresh state, like the remote
  savePtr();
}

static void help() {
  Serial.printf("Switch: t=toggle (=%d flip) | ]/[ power (0x%02X) | +/- flips-per-press | 0-7 pointer | r reset\n",
                REPEATS, PWR[pwr]);
  Serial.println("  Reliability = RF range: keep REPEATS=1, aim the board at the lamp, raise power with ].");
}

void setup() {
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH);
  pinMode(PIN_PA, OUTPUT); pinMode(PIN_LNA, OUTPUT); ampOff();
  pinMode(PIN_GDO0, INPUT); pinMode(PIN_GDO2, INPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);   // BOOT button toggles the lamp, no PC needed
  Serial.begin(115200);
  delay(1500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  radioReset();
  Serial.printf("\nTRANSMIT BOARD v8 (MCSM1 fix): deterministic On/off switch, 250k. PARTNUM 0x%02X (expect 0x80)\n",
                readStatus(0x30));
  radioConfig();
  prefs.begin("velua", false);
  ptr = prefs.getUChar("ptr", 0) % NSTATES;
  pwr = constrain(prefs.getInt("pwr2", 2), 0, NPWR - 1);   // default 0x46; dial down close, up far
  REPEATS = constrain(prefs.getInt("rep", 1), 1, 12);
  applyPwr();
  for (int i = 0; i < 3; i++) { led(0, 0, 40); delay(300); led(0, 0, 0); delay(300); }
  help();
}

void loop() {
  // BOOT button = portable toggle. Power the board from a USB power bank/charger and carry it
  // to the lamp like the remote - press to toggle, the LED flashes red when it sends.
  static bool btnWas = HIGH;
  bool btn = digitalRead(PIN_BTN);
  if (btnWas == HIGH && btn == LOW) {       // press (button pulls to GND)
    delay(20);                              // debounce
    if (digitalRead(PIN_BTN) == LOW) { Serial.println("[button]"); toggleLamp(); }
    while (digitalRead(PIN_BTN) == LOW) delay(5);   // wait for release
  }
  btnWas = btn;

  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') return;
    if (c == 't' || c == 'T' || c == 's' || c == 'S') toggleLamp();
    else if (c == ']') { if (pwr < NPWR - 1) pwr++; applyPwr(); }
    else if (c == '[') { if (pwr > 0) pwr--; applyPwr(); }
    else if (c == '+') { if (REPEATS < 12) REPEATS++; saveRep(); help(); }
    else if (c == '-') { if (REPEATS > 1) REPEATS--; saveRep(); help(); }
    else if (c >= '0' && c <= '7') { ptr = c - '0'; savePtr(); Serial.printf("pointer = %d (%s)\n", ptr, CNT[ptr]); }
    else if (c == 'r' || c == 'R') { ptr = 0; savePtr(); Serial.println("pointer reset to 0"); }
    else if (c == 'h' || c == '?') help();
  }
}