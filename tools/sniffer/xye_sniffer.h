// xye_sniffer.h
// Passive XYE bus framer for ESPHome (used by xye-sniffer.yaml).
//
// It never transmits on its own. The only thing that can ever be sent is the
// read-only C0 status query, and only when the "XYE Query Poll" switch is on.
//
// Framing: an XYE frame starts with 0xAA and ends with 0x55, and the byte
// before 0x55 is a checksum: 0xFF - (sum of every other byte in the frame).
// Rather than assuming 16/32-byte lengths, the framer accepts any length
// 6..64 whose checksum is valid, so unknown frame types (wired-controller
// "follow me" C6 frames, lock/unlock, other variants) are still captured.
// Anything that does not frame cleanly is emitted as a "frag" so nothing on
// the wire is silently dropped.

#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace xye {

struct Frame {
  uint32_t seq = 0;
  uint32_t ms = 0;            // millis() when the frame completed (ESP clock)
  std::string src;            // bus | esp_tx | echo | frag
  std::vector<uint8_t> data;
  bool crc_ok = false;
};

// Checksum per XYE: 0xFF - (sum of all bytes except the checksum byte itself).
// The checksum sits at index n-2, just before the 0x55 terminator.
inline uint8_t checksum(const uint8_t *d, size_t n) {
  uint32_t s = 0;
  for (size_t i = 0; i < n; i++)
    if (i != n - 2) s += d[i];
  return static_cast<uint8_t>(0xFF - (s & 0xFF));
}

inline std::string to_hex(const std::vector<uint8_t> &v) {
  static const char *H = "0123456789ABCDEF";
  std::string s;
  s.reserve(v.size() * 3);
  for (size_t i = 0; i < v.size(); i++) {
    if (i) s.push_back(' ');
    s.push_back(H[v[i] >> 4]);
    s.push_back(H[v[i] & 0x0F]);
  }
  return s;
}

// The query the original firmware used (known to get a 32-byte reply).
static const uint8_t QUERY_C0[16] = {0xAA, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x81, 0x55};

class Sniffer {
 public:
  // --- tunables ---
  uint32_t gap_ms = 50;          // idle time that closes an unfinished frame
  size_t max_len = 64;           // longer than this = not a frame
  uint32_t echo_window_ms = 400; // RX copy of our own TX within this window = echo

  // --- stats ---
  uint32_t seq = 0;
  uint32_t bytes = 0, frames_ok = 0, frags = 0, echoes = 0;
  uint32_t foreign_cmds = 0;     // command-length frames NOT sent by this ESP
  uint32_t last_byte_ms = 0;

  // last good 32-byte reply (for the candidate decode sensors)
  uint8_t resp[32] = {0};
  bool have_resp = false;
  uint32_t resp_count = 0;

  // last command-length frame seen from another bus master
  std::string last_foreign;

  std::deque<Frame> out;         // frames waiting to be published

  void feed(uint8_t b, uint32_t now) {
    bytes++;
    last_byte_ms = now;
    if (buf_.empty() && b != 0xAA) {
      junk_.push_back(b);
      if (junk_.size() >= max_len) flush_junk_(now);
      return;
    }
    if (buf_.empty()) {         // b == 0xAA: start of a frame
      flush_junk_(now);
      buf_.push_back(b);
      return;
    }
    buf_.push_back(b);
    const size_t n = buf_.size();
    if (b == 0x55 && n >= 6 && buf_[n - 2] == checksum(buf_.data(), n)) {
      emit_(buf_, true, now);
      buf_.clear();
      return;
    }
    if (n >= max_len) {
      emit_(buf_, false, now);
      buf_.clear();
    }
  }

  // Call after draining the UART each loop.
  void poll(uint32_t now) {
    if ((now - last_byte_ms) > gap_ms) {
      if (!buf_.empty()) {
        emit_(buf_, false, now);
        buf_.clear();
      }
      flush_junk_(now);
    }
  }

  bool bus_idle(uint32_t now, uint32_t quiet_ms) const {
    return buf_.empty() && junk_.empty() && (now - last_byte_ms) > quiet_ms;
  }

  // Record a frame this ESP put on the wire (so it is logged, and so its
  // echo can be recognised if the transceiver hears itself).
  void note_tx(const uint8_t *d, size_t n, uint32_t now) {
    last_tx_.assign(d, d + n);
    last_tx_ms_ = now;
    Frame f;
    f.seq = ++seq;
    f.ms = now;
    f.src = "esp_tx";
    f.data = last_tx_;
    f.crc_ok = true;
    push_(f);
  }

 private:
  std::vector<uint8_t> buf_, junk_, last_tx_;
  uint32_t last_tx_ms_ = 0;

  void push_(Frame &f) {
    if (out.size() >= 200) out.pop_front();  // never grow without bound
    out.push_back(std::move(f));
  }

  void flush_junk_(uint32_t now) {
    if (junk_.empty()) return;
    emit_(junk_, false, now);
    junk_.clear();
  }

  void emit_(const std::vector<uint8_t> &d, bool ok, uint32_t now) {
    Frame f;
    f.seq = ++seq;
    f.ms = now;
    f.data = d;
    f.crc_ok = ok;
    if (!ok) {
      f.src = "frag";
      frags++;
    } else if (!last_tx_.empty() && d == last_tx_ && (now - last_tx_ms_) < echo_window_ms) {
      f.src = "echo";
      echoes++;
      frames_ok++;
    } else {
      f.src = "bus";
      frames_ok++;
      if (d.size() == 32) {
        for (size_t i = 0; i < 32; i++) resp[i] = d[i];
        have_resp = true;
        resp_count++;
      } else if (d.size() <= 24) {
        // Shorter frames are controller->unit commands. If this ESP did not
        // send it, another master (wired controller / thermostat / central
        // controller) shares this bus.
        foreign_cmds++;
        last_foreign = to_hex(d);
      }
    }
    push_(f);
  }
};

}  // namespace xye

// One global instance, shared by the YAML lambdas.
static xye::Sniffer xye_sniffer;

// XYE temperature byte -> degrees F.
// Confirmed on this unit (set to Fahrenheit) on 2026-11-11: the reply carried
// setpoint 0x42 / T1 0x42 / T2A 0x42 / T3 0x37 while the Midea integration read
// 19.0 C (66.2 F) setpoint, indoor and coil, and 13 C (55.4 F) outdoor coil.
// So temperatures are plain whole degrees F, NOT the (raw - 0x28) / 2 C
// encoding documented for units set to Celsius. 0x00 / 0xFF = no sensor.
inline float xye_temp_f(uint8_t raw) {
  if (raw == 0x00 || raw == 0xFF) return NAN;
  return static_cast<float>(raw);
}
