#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace velua_remote {

// Lindby Velua remote clone on a CC2500 (+ CC2591 PA/LNA) module.
// Protocol: see docs/protocol.md in the repository.
class VeluaRemote : public Component,
                    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                          spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_4MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_pa_pin(GPIOPin *pin) { this->pa_pin_ = pin; }
  void set_lna_pin(GPIOPin *pin) { this->lna_pin_ = pin; }
  void set_pa_table(uint8_t value) { this->pa_table_ = value; }

  // button 1-8. press_ms = how long the button is "held" (100 ms = a normal tap).
  // Blocks for press_ms + 1.6 s while it transmits.
  void send(int button, uint32_t press_ms);
  void tap(int button) { this->send(button, 100); }

 protected:
  void strobe_(uint8_t cmd);
  void write_reg_(uint8_t addr, uint8_t value);
  uint8_t read_reg_(uint8_t addr);
  uint8_t read_status_(uint8_t addr);
  void write_fifo_(const uint8_t *data, uint8_t len);
  uint8_t marc_state_();
  void wait_state_(uint8_t state, uint32_t timeout_us);

  void wake_();
  void configure_();
  void calibrate_();
  void hop_to_(int ch);
  void send_one_(const uint8_t *pkt, uint32_t at);
  void send_for_(const uint8_t *pkt, uint32_t ms, int &ch, int &slot, uint32_t &next);

  GPIOPin *pa_pin_{nullptr};
  GPIOPin *lna_pin_{nullptr};
  uint8_t pa_table_{0x60};
  uint8_t counter_{0};
  bool busy_{false};

  uint32_t ch_word_[3]{};
  uint8_t ch_cal_[3][3]{};

  uint32_t st_pkts_{0}, st_late_{0}, st_first_us_{0}, st_last_us_{0};
};

}  // namespace velua_remote
}  // namespace esphome
