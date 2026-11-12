// Local control of a Midea-family indoor unit over the XYE (RS-485) bus.
//
// Byte maps confirmed on an ACiQ (Midea) ducted air handler, 2026-11 (sweep + C3 test):
//   C0 query  : AA C0 00 00 80 00 00 00 00 00 00 00 00 3F 81 55
//   C3 set    : AA C3 00 00 80 00 MODE FAN SETF 00 00 00 00 3C CRC 55
//   reply (32): b8 mode, b9 fan, b10 setpoint F, b11 T1, b12 T2A (clamped 99),
//               b13 T2B, b14 T3, b19 compressor, b20 flags, b21 op flags,
//               b22/23 error, b24/25 protection
//   CRC = 0xFF - (sum of every other byte)
//
// Safety rules:
//   * Nothing but the read-only C0 query is ever sent until the unit has
//     answered at least once, so a SET is always built from real unit state.
//   * A SET is only sent when Home Assistant changes something. Never at boot.
//   * Control can be switched off at runtime (read-only monitor).
#pragma once

#include <string>

#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace xye_climate {

class XyeClimate : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_poll_interval(uint32_t ms) { this->poll_interval_ = ms; }
  void set_setpoint_range_f(int lo, int hi) {
    this->min_f_ = lo;
    this->max_f_ = hi;
  }
  void set_control_enabled(bool en) { this->control_enabled_ = en; }

  // ---- follow-me (C6): unit regulates on a room temperature we send ----
  void set_follow_me_sensor(sensor::Sensor *s) { this->fm_sensor_ = s; }
  void set_follow_me_interval(uint32_t ms) { this->fm_interval_ = ms; }
  void set_follow_me_celsius(bool c) { this->fm_celsius_ = c; }
  void set_follow_me_source_fahrenheit(bool f) { this->fm_src_f_ = f; }
  // Room-temperature source picked at runtime: an HA entity id. A climate.*
  // entity uses its current_temperature attribute, anything else its state.
  // Empty = no follow-me from the ESP (the unit uses the thermostat / its own
  // sensor as configured). Call once at boot, before HA connects.
  void set_follow_me_entity(const std::string &entity);
  const std::string &follow_me_entity() const { return this->fm_entity_; }
  void set_follow_me_enabled(bool en);
  bool follow_me_enabled() const { return this->fm_enabled_; }
  bool follow_me_active() const { return this->fm_active_; }
  bool has_follow_me_sensor() const { return this->fm_sensor_ != nullptr; }
  // Last temperature sent, in degrees F (NAN if none).
  float follow_me_sent_f() const { return this->fm_sent_f_; }
  bool control_enabled() const { return this->control_enabled_; }

  // Raw access for template sensors in YAML.
  bool has_reply() const { return this->have_reply_; }
  uint8_t reply_byte(int i) const { return (i >= 0 && i < 32) ? this->resp_[i] : 0; }
  uint32_t reply_count() const { return this->reply_count_; }
  uint32_t crc_errors() const { return this->crc_errors_; }
  uint32_t set_count() const { return this->set_count_; }
  uint32_t set_failures() const { return this->set_failures_; }
  uint32_t ms_since_reply() const;
  std::string last_reply_hex() const;
  // Temperature byte -> degrees F (NAN for 0x00 / 0xFF = no sensor).
  float temp_f(int i) const;

  void query_now() { this->force_poll_ = true; }
  // True while another XYE master (wired thermostat) has been heard recently.
  bool shared_bus() const { return this->foreign_ms_ != 0 && millis() - this->foreign_ms_ < 60000; }
  uint32_t foreign_commands() const { return this->foreign_cmds_; }
  // Extended query (C4). Reply kept raw for decoding.
  void query_c4() { this->c4_pending_ = true; }
  std::string last_c4_hex() const;

  // EXPERIMENTAL controls (byte positions inferred from the reply map).
  void set_aux_heat(bool on);
  void start_self_clean();
  bool aux_heat() const { return this->have_reply_ && (this->resp_[20] & 0x02); }
  bool self_cleaning() const { return this->have_reply_ && this->resp_[8] == 0x01; }

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  void read_bus_();
  void handle_frame_(const uint8_t *d, size_t n);
  void apply_reply_();
  void send_(const uint8_t *frame);
  void queue_set_(uint8_t mode_b, uint8_t fan_b, uint8_t sp_f, uint8_t flags_b);
  bool current_cmd_(uint8_t &mode_b, uint8_t &fan_b, uint8_t &sp_f, uint8_t &flags_b) const;
  bool follow_me_due_(uint32_t now);
  bool fm_have_temp_() const;
  float fm_temp_c_() const;
  void send_follow_me_(uint8_t sub, float temp_c);

  static uint8_t checksum_(const uint8_t *d, size_t n);
  static float f_to_c_(float f) { return (f - 32.0f) * 5.0f / 9.0f; }
  static float c_to_f_(float c) { return c * 9.0f / 5.0f + 32.0f; }

  // config
  uint32_t poll_interval_{3000};
  int min_f_{60}, max_f_{86};
  bool control_enabled_{true};

  // rx framer
  uint8_t buf_[40];
  size_t len_{0};
  uint32_t last_rx_ms_{0};

  // last good reply
  uint8_t resp_[32]{};
  bool have_reply_{false};
  bool published_once_{false};
  uint32_t reply_count_{0}, crc_errors_{0};
  uint32_t last_reply_ms_{0};

  // tx state
  uint8_t pending_[16]{};
  bool pending_set_{false};
  uint8_t set_tries_{0};
  bool awaiting_{false};
  bool awaiting_set_{false};
  uint32_t last_tx_ms_{0};
  uint32_t last_poll_ms_{0};
  bool force_poll_{true};
  uint32_t set_count_{0}, set_failures_{0};

  // follow-me
  sensor::Sensor *fm_sensor_{nullptr};
  uint32_t fm_interval_{60000};
  bool fm_celsius_{true};
  bool fm_enabled_{false};
  bool fm_active_{false};
  bool fm_stop_pending_{false};
  uint32_t fm_last_ms_{0};
  float fm_sent_f_{NAN};
  bool fm_src_f_{true};
  bool c4_pending_{false};
  uint8_t last_tx_[16]{};
  uint32_t foreign_ms_{0};
  uint32_t foreign_cmds_{0};
  uint8_t c4_[32]{};
  bool have_c4_{false};
  std::string fm_entity_;
  float fm_entity_c_{NAN};
};

}  // namespace xye_climate
}  // namespace esphome
