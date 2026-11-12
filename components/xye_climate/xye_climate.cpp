#include "xye_climate.h"

#include <cmath>

#include "esphome/components/api/api_server.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace xye_climate {

static const char *const TAG = "xye_climate";

static const uint8_t QUERY_C0[16] = {0xAA, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x81, 0x55};

static const uint32_t REPLY_TIMEOUT_MS = 600;  // 32 bytes @4800 = 67 ms, unit answers in ~130 ms
static const uint32_t BUS_QUIET_MS = 60;
// Shared bus (a wired thermostat also polls on XYE): once another master is
// heard, stop our own polling (its replies keep us in sync) and only talk in
// longer gaps.
static const uint32_t SHARED_HOLD_MS = 60000;
static const uint32_t SHARED_QUIET_MS = 250;
static const uint8_t MAX_SET_TRIES = 3;

// C6 follow-me sub-commands (byte 10)
static const uint8_t FM_START = 0x06;
static const uint8_t FM_UPDATE = 0x02;
static const uint8_t FM_STOP = 0x04;

// mode byte values (command byte 6 == reply byte 8)
static const uint8_t MODE_OFF = 0x00;
static const uint8_t MODE_FAN = 0x81;
static const uint8_t MODE_DRY = 0x82;
static const uint8_t MODE_HEAT = 0x84;
static const uint8_t MODE_COOL = 0x88;
static const uint8_t MODE_AUTO = 0x91;
// fan byte values (command byte 7; reply byte 9 = these, auto adds 0x80)
static const uint8_t FAN_AUTO = 0x80;
static const uint8_t FAN_HIGH = 0x01;
static const uint8_t FAN_MED = 0x02;
static const uint8_t FAN_LOW = 0x04;

uint8_t XyeClimate::checksum_(const uint8_t *d, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i < n; i++)
    if (i != n - 2)
      s += d[i];
  return static_cast<uint8_t>(0xFF - (s & 0xFF));
}

void XyeClimate::setup() {
  // Nothing is sent here. The first C0 poll happens from loop().
  this->mode = climate::CLIMATE_MODE_OFF;
  this->action = climate::CLIMATE_ACTION_OFF;
}

void XyeClimate::dump_config() {
  LOG_CLIMATE("", "XYE Climate", this);
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", (unsigned) this->poll_interval_);
  ESP_LOGCONFIG(TAG, "  Setpoint range: %d..%d F", this->min_f_, this->max_f_);
  if (this->fm_sensor_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Follow-me: every %u ms, temp byte in %s", (unsigned) this->fm_interval_,
                  this->fm_celsius_ ? "C" : "F");
  this->check_uart_settings(4800);
}

climate::ClimateTraits XyeClimate::traits() {
  climate::ClimateTraits t;
  t.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  t.add_supported_mode(climate::CLIMATE_MODE_OFF);
  t.add_supported_mode(climate::CLIMATE_MODE_HEAT_COOL);
  t.add_supported_mode(climate::CLIMATE_MODE_COOL);
  t.add_supported_mode(climate::CLIMATE_MODE_HEAT);
  t.add_supported_mode(climate::CLIMATE_MODE_DRY);
  t.add_supported_mode(climate::CLIMATE_MODE_FAN_ONLY);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_LOW);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_MEDIUM);
  t.add_supported_fan_mode(climate::CLIMATE_FAN_HIGH);
  t.set_visual_min_temperature(f_to_c_(this->min_f_));
  t.set_visual_max_temperature(f_to_c_(this->max_f_));
  t.set_visual_target_temperature_step(0.5f);
  t.set_visual_current_temperature_step(0.5f);
  return t;
}

uint32_t XyeClimate::ms_since_reply() const {
  return this->have_reply_ ? millis() - this->last_reply_ms_ : UINT32_MAX;
}

std::string XyeClimate::last_reply_hex() const {
  if (!this->have_reply_)
    return "none";
  static const char *H = "0123456789ABCDEF";
  std::string s;
  s.reserve(96);
  for (int i = 0; i < 32; i++) {
    if (i)
      s.push_back(' ');
    s.push_back(H[this->resp_[i] >> 4]);
    s.push_back(H[this->resp_[i] & 0x0F]);
  }
  return s;
}

std::string XyeClimate::last_c4_hex() const {
  if (!this->have_c4_)
    return "none";
  static const char *H = "0123456789ABCDEF";
  std::string s;
  for (int i = 0; i < 32; i++) {
    if (i)
      s.push_back(' ');
    s.push_back(H[this->c4_[i] >> 4]);
    s.push_back(H[this->c4_[i] & 0x0F]);
  }
  return s;
}

float XyeClimate::temp_f(int i) const {
  if (!this->have_reply_)
    return NAN;
  const uint8_t raw = this->reply_byte(i);
  if (raw == 0x00 || raw == 0xFF)
    return NAN;
  return static_cast<float>(raw);
}

// ---------------------------------------------------------------------------
// bus
// ---------------------------------------------------------------------------
void XyeClimate::read_bus_() {
  const uint32_t now = millis();
  if (this->len_ > 0 && now - this->last_rx_ms_ > 50)
    this->len_ = 0;  // stale partial frame
  uint8_t b;
  while (this->available() > 0 && this->read_byte(&b)) {
    this->last_rx_ms_ = millis();
    if (this->len_ == 0 && b != 0xAA)
      continue;
    this->buf_[this->len_++] = b;
    if (b == 0x55 && this->len_ >= 16 && this->buf_[this->len_ - 2] == checksum_(this->buf_, this->len_)) {
      this->handle_frame_(this->buf_, this->len_);
      this->len_ = 0;
    } else if (this->len_ >= sizeof(this->buf_)) {
      this->crc_errors_++;
      this->len_ = 0;
    }
  }
}

void XyeClimate::handle_frame_(const uint8_t *d, size_t n) {
  // 16-byte frames are commands: our own echo, or another master (thermostat).
  if (n == 16 && d[2] != 0x80) {
    const bool echo = memcmp(d, this->last_tx_, 16) == 0 && millis() - this->last_tx_ms_ < 300;
    if (!echo) {
      if (millis() - this->foreign_ms_ > SHARED_HOLD_MS)
        ESP_LOGI(TAG, "Another controller is on this XYE bus; switching to shared-bus mode");
      this->foreign_ms_ = millis();
      this->foreign_cmds_++;
      if (d[1] != 0xC0 && d[1] != 0xC4)
        ESP_LOGI(TAG, "Thermostat sent command %02X", d[1]);
    }
    return;
  }
  if (n != 32 || d[2] != 0x80)
    return;
  if (d[1] != 0xC0 && d[1] != 0xC3) {
    if (d[1] == 0xC6)
      ESP_LOGD(TAG, "Follow-me acknowledged");
    if (d[1] == 0xC4) {
      memcpy(this->c4_, d, 32);
      this->have_c4_ = true;
      ESP_LOGI(TAG, "C4 reply: %s", this->last_c4_hex().c_str());
    }
    this->awaiting_ = false;
    return;
  }
  memcpy(this->resp_, d, 32);
  this->have_reply_ = true;
  this->reply_count_++;
  this->last_reply_ms_ = millis();
  if (this->awaiting_set_ && d[1] == 0xC3) {
    // This unit's C3 reply already carries the new state.
    this->pending_set_ = false;
    this->awaiting_set_ = false;
    this->set_count_++;
    ESP_LOGI(TAG, "SET acknowledged");
  }
  this->awaiting_ = false;
  this->apply_reply_();
}

void XyeClimate::send_(const uint8_t *frame) {
  memcpy(this->last_tx_, frame, 16);
  this->write_array(frame, 16);
  this->flush();
  this->last_tx_ms_ = millis();
  this->awaiting_ = true;
}

void XyeClimate::loop() {
  this->read_bus_();
  const uint32_t now = millis();

  if (this->awaiting_) {
    if (now - this->last_tx_ms_ < REPLY_TIMEOUT_MS)
      return;
    this->awaiting_ = false;  // timed out
    if (this->awaiting_set_) {
      this->awaiting_set_ = false;
      if (this->set_tries_ >= MAX_SET_TRIES) {
        ESP_LOGW(TAG, "SET got no reply after %u tries, giving up", (unsigned) this->set_tries_);
        this->pending_set_ = false;
        this->set_failures_++;
        this->force_poll_ = true;  // re-sync HA with the unit
      }
    }
  }
  const uint32_t quiet = this->shared_bus() ? SHARED_QUIET_MS : BUS_QUIET_MS;
  if (this->len_ > 0 || now - this->last_rx_ms_ < quiet || now - this->last_tx_ms_ < quiet)
    return;

  if (this->pending_set_ && this->control_enabled_) {
    this->set_tries_++;
    ESP_LOGI(TAG, "SET mode=%02X fan=%02X sp=%uF (try %u)", this->pending_[6], this->pending_[7],
             (unsigned) this->pending_[8], (unsigned) this->set_tries_);
    this->awaiting_set_ = true;
    this->send_(this->pending_);
    return;
  }
  if (this->follow_me_due_(now))
    return;
  if (this->c4_pending_) {
    static const uint8_t QUERY_C4[16] = {0xAA, 0xC4, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x81, 0x55};
    this->c4_pending_ = false;
    ESP_LOGI(TAG, "Sending C4 query");
    this->send_(QUERY_C4);
    return;
  }
  // Shared bus: the thermostat's own polls keep us updated. Only poll if its
  // replies stop arriving.
  if (this->shared_bus() && !this->force_poll_ && now - this->last_reply_ms_ < 3 * this->poll_interval_)
    return;
  if (this->force_poll_ || now - this->last_poll_ms_ >= this->poll_interval_) {
    this->force_poll_ = false;
    this->last_poll_ms_ = now;
    this->send_(QUERY_C0);
  }
}

// ---------------------------------------------------------------------------
// follow-me
// ---------------------------------------------------------------------------
void XyeClimate::set_follow_me_enabled(bool en) {
  if (this->fm_enabled_ == en)
    return;
  this->fm_enabled_ = en;
  if (!en && this->fm_active_)
    this->fm_stop_pending_ = true;
  if (en)
    this->fm_last_ms_ = 0;  // send a start as soon as possible
}

void XyeClimate::send_follow_me_(uint8_t sub, float temp_c) {
  int t;
  if (this->fm_celsius_)
    t = static_cast<int>(lroundf(temp_c));
  else
    t = static_cast<int>(lroundf(c_to_f_(temp_c)));
  t = clamp(t, 0, 99);
  uint8_t f[16] = {0xAA, 0xC6, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
                   0x00, 0x00, sub, static_cast<uint8_t>(t), 0x00, 0x39, 0x00, 0x55};
  f[14] = checksum_(f, 16);
  ESP_LOGI(TAG, "Follow-me %s %d%s", sub == FM_START ? "start" : sub == FM_STOP ? "stop" : "update", t,
           this->fm_celsius_ ? "C" : "F");
  this->send_(f);
  this->fm_last_ms_ = millis();
  this->fm_sent_f_ = sub == FM_STOP ? NAN : c_to_f_(temp_c);
}

void XyeClimate::set_follow_me_entity(const std::string &entity) {
  std::string e = entity;
  while (!e.empty() && isspace((unsigned char) e.back()))
    e.pop_back();
  while (!e.empty() && isspace((unsigned char) e.front()))
    e.erase(0, 1);
  this->fm_entity_ = e;
  if (e.empty()) {
    ESP_LOGI(TAG, "Follow-me sensor not set; unit keeps its own / thermostat temperature");
    this->set_follow_me_enabled(false);
    return;
  }
  optional<std::string> attr;
  if (e.rfind("climate.", 0) == 0)
    attr = std::string("current_temperature");
  ESP_LOGI(TAG, "Follow-me sensor: %s%s", e.c_str(), attr.has_value() ? " (current_temperature)" : "");
  api::global_api_server->subscribe_home_assistant_state(
      e, attr, std::function<void(StringRef)>([this](StringRef s) {
        auto v = parse_number<float>(s.str());
        if (!v.has_value()) {
          this->fm_entity_c_ = NAN;  // unavailable / unknown
          return;
        }
        this->fm_entity_c_ = this->fm_src_f_ ? f_to_c_(*v) : *v;
      }));
  this->set_follow_me_enabled(true);
}

bool XyeClimate::fm_have_temp_() const { return !std::isnan(this->fm_temp_c_()); }

float XyeClimate::fm_temp_c_() const {
  if (this->fm_sensor_ != nullptr)
    return this->fm_sensor_->has_state() ? this->fm_sensor_->state : NAN;
  return this->fm_entity_c_;
}

bool XyeClimate::follow_me_due_(uint32_t now) {
  if ((this->fm_sensor_ == nullptr && this->fm_entity_.empty()) || !this->have_reply_ || !this->control_enabled_)
    return false;
  if (this->fm_stop_pending_) {
    this->fm_stop_pending_ = false;
    this->fm_active_ = false;
    this->send_follow_me_(FM_STOP, this->fm_have_temp_() ? this->fm_temp_c_() : 20.0f);
    return true;
  }
  if (!this->fm_enabled_)
    return false;
  const bool have = this->fm_have_temp_();
  // Room sensor unavailable (HA reports NAN): stop, so the unit falls back to its own T1.
  if (this->fm_active_ && !have) {
    ESP_LOGW(TAG, "Room sensor unavailable; stopping follow-me");
    this->fm_stop_pending_ = true;
    return false;
  }
  if (!have)
    return false;
  if (this->fm_active_ && this->fm_last_ms_ != 0 && now - this->fm_last_ms_ < this->fm_interval_)
    return false;
  this->send_follow_me_(this->fm_active_ ? FM_UPDATE : FM_START, this->fm_temp_c_());
  this->fm_active_ = true;
  return true;
}

// ---------------------------------------------------------------------------
// reply -> HA state
// ---------------------------------------------------------------------------
void XyeClimate::apply_reply_() {
  const uint8_t *r = this->resp_;
  // Snapshot so we only publish when something actually changed.
  const auto prev_mode = this->mode;
  const auto prev_action = this->action;
  const auto prev_fan = this->fan_mode;
  const float prev_target = this->target_temperature;
  const float prev_current = this->current_temperature;
  this->current_temperature = std::isnan(this->temp_f(11)) ? NAN : f_to_c_(this->temp_f(11));

  // While a change from HA is still being sent, don't flip the UI back to the
  // old state from an intervening status reply.
  if (!this->pending_set_) {
    const uint8_t m = r[8];
    climate::ClimateMode mode;
    if (!(m & 0x80))
      mode = climate::CLIMATE_MODE_OFF;  // 0x00, 0x10 (off after auto), 0x01 (self-clean)
    else if (m & 0x10)
      mode = climate::CLIMATE_MODE_HEAT_COOL;
    else if (m & 0x08)
      mode = climate::CLIMATE_MODE_COOL;
    else if (m & 0x04)
      mode = climate::CLIMATE_MODE_HEAT;
    else if (m & 0x02)
      mode = climate::CLIMATE_MODE_DRY;
    else
      mode = climate::CLIMATE_MODE_FAN_ONLY;
    this->mode = mode;

    const uint8_t f = r[9];
    if (f & 0x80)
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
    else if (f & FAN_HIGH)
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
    else if (f & FAN_MED)
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
    else if (f & FAN_LOW)
      this->fan_mode = climate::CLIMATE_FAN_LOW;

    if (r[10] != 0x00 && r[10] != 0xFF)
      this->target_temperature = f_to_c_(r[10]);
  }

  const bool comp = r[19] & 0x01;
  const bool fan_running = (r[9] & 0x07) != 0;
  switch (this->mode) {
    case climate::CLIMATE_MODE_OFF:
      this->action = climate::CLIMATE_ACTION_OFF;
      break;
    case climate::CLIMATE_MODE_HEAT:
      this->action = comp ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
      break;
    case climate::CLIMATE_MODE_COOL:
      this->action = comp ? climate::CLIMATE_ACTION_COOLING : climate::CLIMATE_ACTION_IDLE;
      break;
    case climate::CLIMATE_MODE_DRY:
      this->action = comp ? climate::CLIMATE_ACTION_DRYING : climate::CLIMATE_ACTION_IDLE;
      break;
    case climate::CLIMATE_MODE_HEAT_COOL:
      if (comp)
        this->action = (r[21] & 0x04) ? climate::CLIMATE_ACTION_COOLING : climate::CLIMATE_ACTION_HEATING;
      else
        this->action = climate::CLIMATE_ACTION_IDLE;
      break;
    default:
      this->action = fan_running ? climate::CLIMATE_ACTION_FAN : climate::CLIMATE_ACTION_IDLE;
      break;
  }
  auto same = [](float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
  const bool changed = !this->published_once_ || this->mode != prev_mode || this->action != prev_action ||
                       this->fan_mode != prev_fan || !same(this->target_temperature, prev_target) ||
                       !same(this->current_temperature, prev_current);
  if (changed) {
    this->published_once_ = true;
    this->publish_state();
  }
}

// ---------------------------------------------------------------------------
// HA -> SET
// ---------------------------------------------------------------------------
void XyeClimate::control(const climate::ClimateCall &call) {
  if (!this->control_enabled_) {
    ESP_LOGW(TAG, "Control disabled; ignoring change");
    this->publish_state();
    return;
  }
  if (!this->have_reply_) {
    ESP_LOGW(TAG, "No reply from the unit yet; refusing to send a blind SET");
    this->publish_state();
    return;
  }
  // Start from what the unit last reported (or the change still queued).
  const uint8_t *base = this->pending_set_ ? nullptr : this->resp_;
  uint8_t mode_b = base ? ((base[8] & 0x80) ? base[8] : MODE_OFF) : this->pending_[6];
  uint8_t fan_b = base ? ((base[9] & 0x80) ? FAN_AUTO : (base[9] & 0x07)) : this->pending_[7];
  uint8_t sp_f = base ? base[10] : this->pending_[8];
  // Byte 9 = mode flags, mirrored by reply byte 20 (bit 0 sleep, bit 1 aux heat).
  // Keep whatever is active so a mode/fan/setpoint change doesn't clear them.
  uint8_t flags_b = base ? (base[20] & 0x03) : this->pending_[9];
  if (fan_b == 0)
    fan_b = FAN_AUTO;

  if (call.get_mode().has_value()) {
    const auto m = *call.get_mode();
    switch (m) {
      case climate::CLIMATE_MODE_OFF: mode_b = MODE_OFF; break;
      case climate::CLIMATE_MODE_HEAT_COOL: mode_b = MODE_AUTO; break;
      case climate::CLIMATE_MODE_COOL: mode_b = MODE_COOL; break;
      case climate::CLIMATE_MODE_HEAT: mode_b = MODE_HEAT; break;
      case climate::CLIMATE_MODE_DRY: mode_b = MODE_DRY; break;
      case climate::CLIMATE_MODE_FAN_ONLY: mode_b = MODE_FAN; break;
      default: break;
    }
    this->mode = m;
  }
  if (call.get_fan_mode().has_value()) {
    const auto f = *call.get_fan_mode();
    switch (f) {
      case climate::CLIMATE_FAN_AUTO: fan_b = FAN_AUTO; break;
      case climate::CLIMATE_FAN_LOW: fan_b = FAN_LOW; break;
      case climate::CLIMATE_FAN_MEDIUM: fan_b = FAN_MED; break;
      case climate::CLIMATE_FAN_HIGH: fan_b = FAN_HIGH; break;
      default: break;
    }
    this->fan_mode = f;
  }
  if (call.get_target_temperature().has_value()) {
    int f = static_cast<int>(lroundf(c_to_f_(*call.get_target_temperature())));
    f = clamp(f, this->min_f_, this->max_f_);
    sp_f = static_cast<uint8_t>(f);
    this->target_temperature = f_to_c_(f);
  }

  this->queue_set_(mode_b, fan_b, sp_f, flags_b);
  this->publish_state();  // optimistic; corrected by the unit's reply
}

void XyeClimate::queue_set_(uint8_t mode_b, uint8_t fan_b, uint8_t sp_f, uint8_t flags_b) {
  const uint8_t frame[16] = {0xAA, 0xC3, 0x00, 0x00, 0x80, 0x00, mode_b, fan_b,
                             sp_f, flags_b, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x55};
  memcpy(this->pending_, frame, 16);
  this->pending_[14] = checksum_(this->pending_, 16);
  this->pending_set_ = true;
  this->set_tries_ = 0;
}

// Current command fields from the last reply (or the change still queued).
bool XyeClimate::current_cmd_(uint8_t &mode_b, uint8_t &fan_b, uint8_t &sp_f, uint8_t &flags_b) const {
  if (!this->have_reply_ || !this->control_enabled_)
    return false;
  if (this->pending_set_) {
    mode_b = this->pending_[6];
    fan_b = this->pending_[7];
    sp_f = this->pending_[8];
    flags_b = this->pending_[9];
    return true;
  }
  const uint8_t *r = this->resp_;
  mode_b = (r[8] & 0x80) ? r[8] : MODE_OFF;
  fan_b = (r[9] & 0x80) ? FAN_AUTO : (r[9] & 0x07);
  if (fan_b == 0)
    fan_b = FAN_AUTO;
  sp_f = r[10];
  flags_b = r[20] & 0x03;
  return true;
}

// EXPERIMENTAL: aux (electric) heat = C3 byte 9 bit 1, inferred from reply b20.
void XyeClimate::set_aux_heat(bool on) {
  uint8_t m, f, s, fl;
  if (!this->current_cmd_(m, f, s, fl)) {
    ESP_LOGW(TAG, "Aux heat: no unit state yet or control disabled");
    return;
  }
  fl = on ? (fl | 0x02) : (fl & ~0x02);
  ESP_LOGI(TAG, "Aux heat %s (flags %02X)", on ? "ON" : "OFF", fl);
  this->queue_set_(m, f, s, fl);
}

// EXPERIMENTAL: self-clean = mode byte 0x01 (what the reply shows while it runs).
// Only from OFF, like the remote/app.
void XyeClimate::start_self_clean() {
  uint8_t m, f, s, fl;
  if (!this->current_cmd_(m, f, s, fl)) {
    ESP_LOGW(TAG, "Self-clean: no unit state yet or control disabled");
    return;
  }
  if (m != MODE_OFF) {
    ESP_LOGW(TAG, "Self-clean: turn the unit OFF first");
    return;
  }
  ESP_LOGI(TAG, "Self-clean start");
  this->queue_set_(0x01, f, s, 0x00);
}

}  // namespace xye_climate
}  // namespace esphome
