#include "velua_remote.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace velua_remote {

static const char *const TAG = "velua_remote";

// ---- protocol (see docs/protocol.md) ---------------------------------------------------------
static const int NSTATES = 8;
static const int NBTN = 8;
static const int FRAME_LEN = 17;
static const int PKT_LEN = 28;  // AA AA AA AA CC CC CC CC B9 A1 99 + 17-byte frame

static const uint8_t REL[NSTATES][FRAME_LEN] = {
    {0x0D, 0xBF, 0x29, 0xB9, 0x22, 0x57, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xED, 0x99, 0x1E, 0xA7, 0x3E, 0xAC},
    {0x0D, 0xBF, 0x29, 0xA8, 0x32, 0xDF, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xFC, 0x89, 0xA0, 0x18, 0xB2, 0xF1},
    {0x0D, 0xBF, 0x29, 0xA9, 0x11, 0x57, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xFD, 0xAA, 0xA1, 0xDA, 0x63, 0xB4},
    {0x0D, 0xBF, 0x29, 0xB8, 0x01, 0xDF, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xEC, 0xBA, 0x1F, 0x65, 0xEF, 0xE9},
    {0x0D, 0xBF, 0x29, 0x8A, 0x02, 0x57, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xDE, 0xB9, 0xC3, 0x26, 0x40, 0x46},
    {0x0D, 0xBF, 0x29, 0x9B, 0x12, 0xDF, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xCF, 0xA9, 0x7D, 0x99, 0xCC, 0x1B},
    {0x0D, 0xBF, 0x29, 0x9A, 0x31, 0x57, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xCE, 0x8A, 0x7C, 0x5B, 0x1D, 0x5E},
    {0x0D, 0xBF, 0x29, 0x8B, 0x21, 0xDF, 0x5E, 0x4B, 0x9C, 0xA1, 0xB6, 0xDF, 0x9A, 0xC2, 0xE4, 0x91, 0x03},
};
static const uint8_t PRS[NSTATES][FRAME_LEN] = {
    {0x0D, 0xBF, 0x29, 0x8B, 0x30, 0x5F, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xDF, 0x9A, 0x4D, 0xDD, 0x6B, 0x2D},
    {0x0D, 0xBF, 0x29, 0xB9, 0x33, 0xD7, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xED, 0x99, 0x91, 0x9E, 0xC4, 0x82},
    {0x0D, 0xBF, 0x29, 0xA8, 0x23, 0x5F, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xFC, 0x89, 0x2F, 0x21, 0x48, 0xDF},
    {0x0D, 0xBF, 0x29, 0xA9, 0x00, 0xD7, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xFD, 0xAA, 0x2E, 0xE3, 0x99, 0x9A},
    {0x0D, 0xBF, 0x29, 0xB8, 0x10, 0x5F, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xEC, 0xBA, 0x90, 0x5C, 0x15, 0xC7},
    {0x0D, 0xBF, 0x29, 0x8A, 0x13, 0xD7, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xDE, 0xB9, 0x4C, 0x1F, 0xBA, 0x68},
    {0x0D, 0xBF, 0x29, 0x9B, 0x03, 0x5F, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xCF, 0xA9, 0xF2, 0xA0, 0x36, 0x35},
    {0x0D, 0xBF, 0x29, 0x9A, 0x20, 0xD7, 0xC7, 0xCB, 0x14, 0xA1, 0xB6, 0xCE, 0x8A, 0xF3, 0x62, 0xE7, 0x70},
};
// press frame of button b = PRS[s] XOR BTN_XOR[b]
static const uint8_t BTN_XOR[NBTN][FRAME_LEN] = {
    {0, 0, 0, 0, 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // 1 On/off
    {0, 0, 0, 0, 0, 0x10, 0x22, 0x80, 0x88, 0x00, 0x00, 0x10, 0x22, 0x49, 0x97, 0xEA, 0x0C},  // 2 Night
    {0, 0, 0, 0, 0, 0x20, 0x77, 0x80, 0x88, 0x00, 0x00, 0x20, 0x77, 0x94, 0x16, 0x19, 0x89},  // 3 Brightness up
    {0, 0, 0, 0, 0, 0x33, 0x31, 0x80, 0x88, 0x00, 0x00, 0x33, 0x31, 0xB7, 0x14, 0xDA, 0xA9},  // 4 Brightness down
    {0, 0, 0, 0, 0, 0x40, 0xDD, 0x80, 0x88, 0x00, 0x00, 0x40, 0xDD, 0xC0, 0x44, 0xAF, 0x4E},  // 5 Cycle colour temp
    {0, 0, 0, 0, 0, 0x00, 0x11, 0x80, 0x99, 0x80, 0x88, 0x00, 0x00, 0x40, 0xDD, 0x5C, 0x3B},  // 6 Timer 60 s
    {0, 0, 0, 0, 0, 0x00, 0x11, 0x90, 0xBB, 0x00, 0x00, 0x10, 0x22, 0x68, 0xD2, 0x24, 0x3B},  // 7 Cooler
    {0, 0, 0, 0, 0, 0x00, 0x11, 0x91, 0x98, 0x88, 0x00, 0x11, 0x01, 0x5D, 0xE8, 0x8B, 0x94},  // 8 Warmer
};
static const uint8_t HEADER[11] = {0xAA, 0xAA, 0xAA, 0xAA, 0xCC, 0xCC, 0xCC, 0xCC, 0xB9, 0xA1, 0x99};

static const float CH_MHZ[3] = {2402.00f, 2448.00f, 2478.95f};
static const int PKTS_PER_CH = 3;
static const uint32_t SLOT_US = 1245;
static const uint32_t HOP_EXTRA_US = 160;
static const uint32_t RELEASE_MS = 1600;
static const uint32_t MAX_PRESS_MS = 10000;

// ---- CC2500 registers -------------------------------------------------------------------------
enum : uint8_t {
  IOCFG2 = 0x00, IOCFG0 = 0x02, FIFOTHR = 0x03, PKTLEN = 0x06, PKTCTRL1 = 0x07, PKTCTRL0 = 0x08,
  ADDR = 0x09, CHANNR = 0x0A, FSCTRL1 = 0x0B, FSCTRL0 = 0x0C, FREQ2 = 0x0D, FREQ1 = 0x0E, FREQ0 = 0x0F,
  MDMCFG4 = 0x10, MDMCFG3 = 0x11, MDMCFG2 = 0x12, MDMCFG1 = 0x13, MDMCFG0 = 0x14, DEVIATN = 0x15,
  MCSM1 = 0x17, MCSM0 = 0x18, FOCCFG = 0x19, BSCFG = 0x1A, AGCCTRL2 = 0x1B, AGCCTRL1 = 0x1C,
  AGCCTRL0 = 0x1D, FREND1 = 0x21, FREND0 = 0x22, FSCAL3 = 0x23, FSCAL2 = 0x24, FSCAL1 = 0x25,
  FSCAL0 = 0x26, TEST2 = 0x2C, TEST1 = 0x2D, TEST0 = 0x2E, PATABLE = 0x3E, TXFIFO = 0x3F,
  SRES = 0x30, SFSTXON = 0x31, SCAL = 0x33, STX = 0x35, SIDLE = 0x36, SPWD = 0x39, SFTX = 0x3B,
  PARTNUM = 0x30, MARCSTATE = 0x35,
};
enum : uint8_t { ST_IDLE = 0x01, ST_FSTXON = 0x12, ST_TX = 0x13, ST_TX_UNDERFLOW = 0x16 };

void VeluaRemote::strobe_(uint8_t cmd) {
  this->enable();
  this->transfer_byte(cmd);
  this->disable();
}
void VeluaRemote::write_reg_(uint8_t addr, uint8_t value) {
  this->enable();
  this->transfer_byte(addr);
  this->transfer_byte(value);
  this->disable();
}
uint8_t VeluaRemote::read_reg_(uint8_t addr) {
  this->enable();
  this->transfer_byte(addr | 0x80);
  uint8_t v = this->transfer_byte(0);
  this->disable();
  return v;
}
uint8_t VeluaRemote::read_status_(uint8_t addr) {
  this->enable();
  this->transfer_byte(addr | 0xC0);
  uint8_t v = this->transfer_byte(0);
  this->disable();
  return v;
}
void VeluaRemote::write_fifo_(const uint8_t *data, uint8_t len) {
  this->enable();
  this->transfer_byte(TXFIFO | 0x40);  // burst write
  for (uint8_t i = 0; i < len; i++)
    this->transfer_byte(data[i]);
  this->disable();
}
uint8_t VeluaRemote::marc_state_() { return this->read_status_(MARCSTATE) & 0x1F; }
void VeluaRemote::wait_state_(uint8_t state, uint32_t timeout_us) {
  uint32_t t = micros();
  while (this->marc_state_() != state && micros() - t < timeout_us) {
  }
}

// Wakes the chip from SLEEP: CSn low starts the crystal; give it time before talking to it.
void VeluaRemote::wake_() {
  this->enable();
  delayMicroseconds(500);
  this->disable();
  delayMicroseconds(50);
}

// Full register setup. Also needed after SLEEP, because PATABLE and TEST0-2 are lost there.
void VeluaRemote::configure_() {
  this->strobe_(SIDLE);
  this->wait_state_(ST_IDLE, 2000);
  this->write_reg_(IOCFG2, 0x29);
  this->write_reg_(IOCFG0, 0x06);
  this->write_reg_(FIFOTHR, 0x07);
  this->write_reg_(PKTLEN, PKT_LEN);
  this->write_reg_(PKTCTRL1, 0x00);
  this->write_reg_(PKTCTRL0, 0x00);  // fixed length, no CRC, no whitening
  this->write_reg_(ADDR, 0x00);
  this->write_reg_(CHANNR, 0x00);
  this->write_reg_(FSCTRL1, 0x10);
  this->write_reg_(FSCTRL0, 0x00);
  this->write_reg_(MDMCFG4, 0x0D);  // 250 kbps
  this->write_reg_(MDMCFG3, 0x3B);
  this->write_reg_(MDMCFG2, 0x00);  // 2-FSK, no chip preamble/sync (it is in the packet)
  this->write_reg_(MDMCFG1, 0x22);
  this->write_reg_(MDMCFG0, 0xF8);
  this->write_reg_(DEVIATN, 0x73);  // ~279 kHz
  this->write_reg_(MCSM1, 0x31);    // after TX -> FSTXON (0x32 would keep the carrier on)
  this->write_reg_(MCSM0, 0x08);    // manual calibration
  this->write_reg_(FOCCFG, 0x1D);
  this->write_reg_(BSCFG, 0x1C);
  this->write_reg_(AGCCTRL2, 0xC7);
  this->write_reg_(AGCCTRL1, 0x40);
  this->write_reg_(AGCCTRL0, 0xB2);
  this->write_reg_(FREND1, 0xB6);
  this->write_reg_(FREND0, 0x10);
  this->write_reg_(FSCAL3, 0xEA);
  this->write_reg_(FSCAL2, 0x0A);
  this->write_reg_(FSCAL1, 0x00);
  this->write_reg_(FSCAL0, 0x11);
  this->write_reg_(TEST2, 0x88);
  this->write_reg_(TEST1, 0x31);
  this->write_reg_(TEST0, 0x0B);
  this->write_reg_(PATABLE, this->pa_table_);
}

// Calibrate each channel once and keep the results, so hopping needs no calibration.
void VeluaRemote::calibrate_() {
  for (int i = 0; i < 3; i++) {
    this->ch_word_[i] = (uint32_t) ((double) CH_MHZ[i] * 1e6 * 65536.0 / 26e6 + 0.5);
    this->strobe_(SIDLE);
    this->wait_state_(ST_IDLE, 2000);
    this->write_reg_(FREQ2, (this->ch_word_[i] >> 16) & 0xFF);
    this->write_reg_(FREQ1, (this->ch_word_[i] >> 8) & 0xFF);
    this->write_reg_(FREQ0, this->ch_word_[i] & 0xFF);
    this->strobe_(SCAL);
    delayMicroseconds(50);
    this->wait_state_(ST_IDLE, 3000);
    this->ch_cal_[i][0] = this->read_reg_(FSCAL3);
    this->ch_cal_[i][1] = this->read_reg_(FSCAL2);
    this->ch_cal_[i][2] = this->read_reg_(FSCAL1);
  }
}

void VeluaRemote::hop_to_(int ch) {
  this->strobe_(SIDLE);
  this->wait_state_(ST_IDLE, 500);
  this->write_reg_(FREQ2, (this->ch_word_[ch] >> 16) & 0xFF);
  this->write_reg_(FREQ1, (this->ch_word_[ch] >> 8) & 0xFF);
  this->write_reg_(FREQ0, this->ch_word_[ch] & 0xFF);
  this->write_reg_(FSCAL3, this->ch_cal_[ch][0]);
  this->write_reg_(FSCAL2, this->ch_cal_[ch][1]);
  this->write_reg_(FSCAL1, this->ch_cal_[ch][2]);
  this->strobe_(SFSTXON);
}

void VeluaRemote::send_one_(const uint8_t *pkt, uint32_t at) {
  this->write_fifo_(pkt, PKT_LEN);
  if ((int32_t) (micros() - at) > 50)
    this->st_late_++;
  while ((int32_t) (micros() - at) < 0) {
  }
  uint32_t t = micros();
  this->strobe_(STX);
  if (this->st_pkts_ == 0)
    this->st_first_us_ = t;
  this->st_last_us_ = t;
  while (this->marc_state_() != ST_TX && micros() - t < 400) {
  }
  while (this->marc_state_() == ST_TX && micros() - t < 3000) {
  }
  this->st_pkts_++;
  if (this->marc_state_() == ST_TX_UNDERFLOW) {
    this->strobe_(SIDLE);
    this->strobe_(SFTX);
    this->strobe_(SFSTXON);
  }
}

void VeluaRemote::send_for_(const uint8_t *pkt, uint32_t ms, int &ch, int &slot, uint32_t &next) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (slot == 0)
      this->hop_to_(ch);
    this->send_one_(pkt, next);
    next += SLOT_US;
    if (++slot == PKTS_PER_CH) {
      slot = 0;
      ch = (ch + 1) % 3;
      next += HOP_EXTRA_US;
      App.feed_wdt();
    }
  }
}

void VeluaRemote::setup() {
  this->spi_setup();
  this->pa_pin_->setup();
  this->lna_pin_->setup();
  this->pa_pin_->digital_write(false);
  this->lna_pin_->digital_write(false);

  this->wake_();
  this->strobe_(SRES);
  delay(2);
  uint8_t part = this->read_status_(PARTNUM);
  if (part != 0x80) {
    ESP_LOGE(TAG, "CC2500 not found (PARTNUM 0x%02X, expected 0x80). Check wiring.", part);
    this->mark_failed();
    return;
  }
  this->configure_();
  this->calibrate_();
  this->strobe_(SIDLE);
  this->strobe_(SPWD);  // sleep until the first button press
}

void VeluaRemote::dump_config() {
  ESP_LOGCONFIG(TAG, "Velua remote (CC2500):");
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  PA Pin: ", this->pa_pin_);
  LOG_PIN("  LNA Pin: ", this->lna_pin_);
  ESP_LOGCONFIG(TAG, "  PATABLE: 0x%02X", this->pa_table_);
  if (this->is_failed())
    ESP_LOGE(TAG, "  CC2500 not found");
}

void VeluaRemote::send(int button, uint32_t press_ms) {
  if (this->is_failed() || this->busy_)
    return;
  if (button < 1 || button > NBTN) {
    ESP_LOGW(TAG, "Button %d out of range (1-8)", button);
    return;
  }
  if (press_ms < 50)
    press_ms = 50;
  if (press_ms > MAX_PRESS_MS)
    press_ms = MAX_PRESS_MS;
  this->busy_ = true;

  const int s = this->counter_;
  uint8_t press_pkt[PKT_LEN], release_pkt[PKT_LEN];
  memcpy(press_pkt, HEADER, sizeof(HEADER));
  memcpy(release_pkt, HEADER, sizeof(HEADER));
  for (int i = 0; i < FRAME_LEN; i++) {
    press_pkt[11 + i] = PRS[s][i] ^ BTN_XOR[button - 1][i];
    release_pkt[11 + i] = REL[s][i];
  }

  this->wake_();
  this->configure_();
  this->calibrate_();

  this->lna_pin_->digital_write(false);
  this->pa_pin_->digital_write(true);
  this->st_pkts_ = this->st_late_ = 0;
  int ch = 0, slot = 0;
  uint32_t next = micros() + 300;
  this->send_for_(press_pkt, press_ms, ch, slot, next);
  this->send_for_(release_pkt, RELEASE_MS, ch, slot, next);
  this->strobe_(SIDLE);
  this->pa_pin_->digital_write(false);
  this->strobe_(SPWD);

  this->counter_ = (this->counter_ + 1) % NSTATES;
  this->busy_ = false;
  ESP_LOGI(TAG, "Sent button %d (%u ms press): %u packets, avg period %u us, late %u", button,
           (unsigned) press_ms, (unsigned) this->st_pkts_,
           (unsigned) (this->st_pkts_ > 1 ? (this->st_last_us_ - this->st_first_us_) / (this->st_pkts_ - 1) : 0),
           (unsigned) this->st_late_);
}

}  // namespace velua_remote
}  // namespace esphome
