#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include <cstring>
#include <string>

namespace esphome {
namespace audio_sentinel {

// Ring buffer of (peak dB, noise floor) samples, served over /api/audio_buffer
// so the Home Assistant ApexCharts card can pre-fill the live window on load.
static const int RING_SIZE = 1200;  // 5 min @ 250 ms

struct Sample {
  float peak;
  float nf;
};

// Forward declaration; the HTTP handler reads the hub's ring buffer.
class AudioSentinel;

// GET /api/audio_buffer?count=N -> {"count":N,"ms":MS,"p":[...],"n":[...]}
// Two parallel arrays; timestamps are derived client-side from count & ms.
class AudioBufferHandler : public AsyncWebHandler {
 public:
  explicit AudioBufferHandler(AudioSentinel *parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  AudioSentinel *parent_;
};

class AudioSentinel : public Component {
 public:
  // --- wiring (set from codegen) ---
  void set_rms_sensor(sensor::Sensor *s) { this->rms_sensor_ = s; }
  void set_peak_sensor(sensor::Sensor *s) { this->peak_sensor_ = s; }
  void set_web_server_base(web_server_base::WebServerBase *b) { this->web_server_base_ = b; }
  void set_squawk_number(number::Number *n) { this->squawk_number_ = n; }
  void set_cry_number(number::Number *n) { this->cry_number_ = n; }
  void set_offset_number(number::Number *n) { this->offset_number_ = n; }

  // --- child entities (set from sensor.py / binary_sensor.py) ---
  void set_db_live_sensor(sensor::Sensor *s) { this->db_live_sensor_ = s; }
  void set_peak_live_sensor(sensor::Sensor *s) { this->peak_live_sensor_ = s; }
  void set_peak_events_sensor(sensor::Sensor *s) { this->peak_events_sensor_ = s; }
  void set_noise_floor_sensor(sensor::Sensor *s) { this->noise_floor_sensor_ = s; }
  void set_squawk_threshold_sensor(sensor::Sensor *s) { this->squawk_threshold_sensor_ = s; }
  void set_cry_threshold_sensor(sensor::Sensor *s) { this->cry_threshold_sensor_ = s; }
  void set_est_db_sensor(sensor::Sensor *s) { this->est_db_sensor_ = s; }
  void set_squawk_binary_sensor(binary_sensor::BinarySensor *s) { this->squawk_binary_sensor_ = s; }
  void set_cry_binary_sensor(binary_sensor::BinarySensor *s) { this->cry_binary_sensor_ = s; }

  // --- tuning knobs ---
  void set_live_interval(uint32_t ms) { this->live_interval_ = ms; }
  void set_events_interval(uint32_t ms) { this->events_interval_ = ms; }
  void set_initial_floor_db(float v) { this->initial_floor_db_ = v; }
  void set_floor_drift_db(float v) { this->floor_drift_db_ = v; }
  void set_margin_db(float v) { this->margin_db_ = v; }
  void set_hysteresis_db(float v) { this->hysteresis_db_ = v; }
  void set_floor_alpha(float v) { this->floor_alpha_ = v; }
  void set_release_coeff(float v) { this->release_coeff_ = v; }
  void set_attack_coeff(float v) { this->attack_coeff_ = v; }
  void set_hold_ms(uint32_t v) { this->hold_ms_ = v; }
  void set_glide(float v) { this->glide_ = v; }
  void set_attack_db(float v) { this->attack_db_ = v; }

  // --- runtime audio gate (HA switch / OTA) ---
  void set_audio_enabled(bool en) { this->audio_enabled_ = en; }
  bool is_audio_enabled() const { return this->audio_enabled_; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Ring buffer access for the HTTP handler.
  const Sample *ring() const { return this->ring_; }
  int ring_count() const { return this->ring_cnt_; }
  int ring_wr() const { return this->ring_wr_; }
  uint32_t sample_ms() const { return this->live_interval_; }

 protected:
  void live_tick_();    // every live_interval_: envelope, adaptive floor, ring push, live + floor publish, binary sensors
  void events_tick_();  // every events_interval_: peak-hold envelope -> events series
  void ring_push_(float peak, float nf);
  bool hysteresis_(float value, float threshold, bool &state) const;

  // wiring
  sensor::Sensor *rms_sensor_{nullptr};
  sensor::Sensor *peak_sensor_{nullptr};
  web_server_base::WebServerBase *web_server_base_{nullptr};
  number::Number *squawk_number_{nullptr};
  number::Number *cry_number_{nullptr};
  number::Number *offset_number_{nullptr};

  // child entities
  sensor::Sensor *db_live_sensor_{nullptr};
  sensor::Sensor *peak_live_sensor_{nullptr};
  sensor::Sensor *peak_events_sensor_{nullptr};
  sensor::Sensor *noise_floor_sensor_{nullptr};
  sensor::Sensor *squawk_threshold_sensor_{nullptr};
  sensor::Sensor *cry_threshold_sensor_{nullptr};
  sensor::Sensor *est_db_sensor_{nullptr};
  binary_sensor::BinarySensor *squawk_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *cry_binary_sensor_{nullptr};

  // tuning (defaults mirror the original lambdas)
  uint32_t live_interval_{250};
  uint32_t events_interval_{2000};
  float initial_floor_db_{-60.0f};
  float floor_drift_db_{0.001f};
  float margin_db_{2.0f};
  float hysteresis_db_{1.5f};
  float floor_alpha_{0.005f};
  float release_coeff_{0.15f};
  float attack_coeff_{1.0f};  // live envelope attack EMA; 1.0 = instant (legacy)
  uint32_t hold_ms_{20000};
  float glide_{0.10f};
  float attack_db_{0.5f};

  // ring buffer
  Sample ring_[RING_SIZE];
  int ring_wr_{0};
  int ring_cnt_{0};

  // live DSP state (was function-static in spl_peak_live lambda)
  float env_{NAN};       // smoothed envelope
  float floor_db_{NAN};  // adaptive noise floor

  // events DSP state (was function-static in spl_peak_events lambda)
  float ev_u_{NAN};      // input smoother
  float ev_bl_{NAN};     // slow baseline
  float ev_s_{NAN};      // peak-hold envelope
  uint32_t ev_t_peak_{0};
  bool ev_was_above_{false};

  // binary sensor hysteresis latches
  bool squawk_on_{false};
  bool cry_on_{false};

  // audio gate
  bool audio_enabled_{true};
};

}  // namespace audio_sentinel
}  // namespace esphome
