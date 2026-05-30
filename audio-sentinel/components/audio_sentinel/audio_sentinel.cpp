#include "audio_sentinel.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>

namespace esphome {
namespace audio_sentinel {

static const char *const TAG = "audio_sentinel";

// ───────────────────────── HTTP handler ─────────────────────────
bool AudioBufferHandler::canHandle(AsyncWebServerRequest *request) const {
  char buf[513];
  request->url_to(std::span<char, 513>(buf));
  return request->method() == HTTP_GET && strcmp(buf, "/api/audio_buffer") == 0;
}

void AudioBufferHandler::handleRequest(AsyncWebServerRequest *request) {
  const Sample *ring = this->parent_->ring();
  const int ring_cnt = this->parent_->ring_count();
  const int snap_wr = this->parent_->ring_wr();
  const uint32_t ms = this->parent_->sample_ms();

  int n = 480;  // default: 2 min
  if (request->hasParam("count")) {
    n = atoi(request->getParam("count")->value().c_str());
    if (n <= 0)
      n = 480;
    if (n > RING_SIZE)
      n = RING_SIZE;
  }

  int avail = (ring_cnt < n) ? ring_cnt : n;
  int pad = n - avail;

  std::string out;
  out.reserve(n * 11 + 64);
  char tmp[24];

  snprintf(tmp, sizeof(tmp), "{\"count\":%d,", n);
  out += tmp;
  snprintf(tmp, sizeof(tmp), "\"ms\":%u,", (unsigned) ms);
  out += tmp;

  // p[] — peak values
  out += "\"p\":[";
  for (int i = 0; i < n; i++) {
    if (i)
      out += ',';
    if (i < pad) {
      out += '0';
    } else {
      int ri = (snap_wr - avail + (i - pad) + RING_SIZE) % RING_SIZE;
      snprintf(tmp, sizeof(tmp), "%.1f", ring[ri].peak);
      out += tmp;
    }
  }

  // n[] — noise floor values
  out += "],\"n\":[";
  for (int i = 0; i < n; i++) {
    if (i)
      out += ',';
    if (i < pad) {
      out += '0';
    } else {
      int ri = (snap_wr - avail + (i - pad) + RING_SIZE) % RING_SIZE;
      snprintf(tmp, sizeof(tmp), "%.1f", ring[ri].nf);
      out += tmp;
    }
  }
  out += "]}";

  auto *resp = request->beginResponse(200, "application/json", out.c_str());
  resp->addHeader("Access-Control-Allow-Origin", "*");
  request->send(resp);
}

// ───────────────────────── component ─────────────────────────
void AudioSentinel::setup() {
  this->floor_db_ = this->initial_floor_db_;

  if (this->web_server_base_ != nullptr) {
    this->web_server_base_->add_handler(new AudioBufferHandler(this));
  } else {
    ESP_LOGW(TAG, "no web_server_base — /api/audio_buffer disabled");
  }

  this->set_interval("live", this->live_interval_, [this]() { this->live_tick_(); });
  this->set_interval("events", this->events_interval_, [this]() { this->events_tick_(); });
}

void AudioSentinel::ring_push_(float peak, float nf) {
  this->ring_[this->ring_wr_] = {peak, nf};
  this->ring_wr_ = (this->ring_wr_ + 1) % RING_SIZE;
  if (this->ring_cnt_ < RING_SIZE)
    this->ring_cnt_++;
}

bool AudioSentinel::hysteresis_(float value, float threshold, bool &state) const {
  if (std::isnan(value))
    return state;
  if (state) {
    if (value <= threshold - this->hysteresis_db_)
      state = false;
  } else {
    if (value >= threshold)
      state = true;
  }
  return state;
}

// Live tick: smoothed envelope -> adaptive noise floor -> gate -> ring + live publish.
// Ports the spl_peak_live + noise_floor lambdas.
void AudioSentinel::live_tick_() {
  // Threshold mirrors stay visible even when audio is off.
  if (this->squawk_threshold_sensor_ != nullptr && this->squawk_number_ != nullptr)
    this->squawk_threshold_sensor_->publish_state(this->squawk_number_->state);
  if (this->cry_threshold_sensor_ != nullptr && this->cry_number_ != nullptr)
    this->cry_threshold_sensor_->publish_state(this->cry_number_->state);

  if (!this->audio_enabled_) {
    this->ring_push_(0, 0);
    if (this->db_live_sensor_ != nullptr)
      this->db_live_sensor_->publish_state(NAN);
    if (this->peak_live_sensor_ != nullptr)
      this->peak_live_sensor_->publish_state(NAN);
    if (this->squawk_binary_sensor_ != nullptr)
      this->squawk_binary_sensor_->publish_state(false);
    if (this->cry_binary_sensor_ != nullptr)
      this->cry_binary_sensor_->publish_state(false);
    return;
  }

  // db (live): mirror RMS; filtering (throttle/delta) lives on the YAML sensor.
  if (this->db_live_sensor_ != nullptr && this->rms_sensor_ != nullptr)
    this->db_live_sensor_->publish_state(this->rms_sensor_->state);

  // est dB ≈ RMS + offset.
  if (this->est_db_sensor_ != nullptr && this->rms_sensor_ != nullptr && this->offset_number_ != nullptr)
    this->est_db_sensor_->publish_state(this->rms_sensor_->state + this->offset_number_->state);

  const float v = (this->peak_sensor_ != nullptr) ? this->peak_sensor_->state : NAN;
  if (std::isnan(v)) {
    this->ring_push_(0, 0);
    if (this->peak_live_sensor_ != nullptr)
      this->peak_live_sensor_->publish_state(NAN);
    return;
  }

  // Binary sensors trigger on the raw peak with hysteresis.
  if (this->squawk_binary_sensor_ != nullptr && this->squawk_number_ != nullptr)
    this->squawk_binary_sensor_->publish_state(this->hysteresis_(v, this->squawk_number_->state, this->squawk_on_));
  if (this->cry_binary_sensor_ != nullptr && this->cry_number_ != nullptr)
    this->cry_binary_sensor_->publish_state(this->hysteresis_(v, this->cry_number_->state, this->cry_on_));

  // Smooth envelope: instant attack, moderate release.
  if (std::isnan(this->env_))
    this->env_ = v;
  if (v > this->env_) {
    this->env_ = v;  // instant attack — cry shows immediately
  } else {
    this->env_ += this->release_coeff_ * (v - this->env_);
  }
  const float s = this->env_;

  // Adaptive noise floor: gated EMA + slow ungated upward drift.
  const float margin = this->margin_db_;
  if (s <= this->floor_db_ + margin) {
    this->floor_db_ += this->floor_alpha_ * (s - this->floor_db_);  // gated: track ambient
  } else {
    this->floor_db_ += this->floor_drift_db_;  // ungated: slow upward
  }
  if (this->noise_floor_sensor_ != nullptr)
    this->noise_floor_sensor_->publish_state(this->floor_db_);

  // Gate: flat floor during quiet, raw signal during events.
  float ret;
  if (s > this->floor_db_ + margin) {
    ret = roundf(s * 10.0f) / 10.0f;  // event: 0.1 dB precision
  } else {
    ret = roundf(this->floor_db_ * 2.0f) / 2.0f;  // quiet: 0.5 dB steps (visually flat)
  }
  this->ring_push_(ret, this->floor_db_);
  if (this->peak_live_sensor_ != nullptr)
    this->peak_live_sensor_->publish_state(ret);
}

// Events tick: peak-hold envelope gliding to a slow baseline; only publishes
// above-floor events (+ one marker when an event ends). Ports spl_peak_events.
void AudioSentinel::events_tick_() {
  if (!this->audio_enabled_)
    return;
  const float v = (this->peak_sensor_ != nullptr) ? this->peak_sensor_->state : NAN;
  if (std::isnan(v) || std::isnan(this->floor_db_))
    return;
  if (this->peak_events_sensor_ == nullptr)
    return;

  const float margin = this->margin_db_;

  // Input pre-smoothing + slow baseline.
  if (std::isnan(this->ev_u_))
    this->ev_u_ = v;
  if (std::isnan(this->ev_bl_))
    this->ev_bl_ = v;
  this->ev_u_ += 0.40f * (v - this->ev_u_);    // light attack smoothing (kills 1-sample blips)
  this->ev_bl_ += 0.03f * (v - this->ev_bl_);  // ~1 min baseline the release glides toward

  // Peak-hold envelope: instant attack, hold, glide to baseline.
  const uint32_t now = millis();
  if (std::isnan(this->ev_s_)) {
    this->ev_s_ = this->ev_u_;
    this->ev_t_peak_ = now;
  }
  if (this->ev_u_ >= this->ev_s_ + this->attack_db_) {
    this->ev_s_ = this->ev_u_;
    this->ev_t_peak_ = now;  // instant attack + re-arm hold
  } else if (now - this->ev_t_peak_ > this->hold_ms_) {
    this->ev_s_ += this->glide_ * (this->ev_bl_ - this->ev_s_);  // glide toward baseline
  }                                                              // else: hold flat

  const bool is_above = (this->ev_s_ > this->floor_db_ + margin);
  if (is_above) {
    this->ev_was_above_ = true;
    this->peak_events_sensor_->publish_state(roundf(this->ev_s_ * 10.0f) / 10.0f);
    return;
  }
  if (this->ev_was_above_) {
    // Event just ended — publish one floor value as marker.
    this->ev_was_above_ = false;
    this->peak_events_sensor_->publish_state(roundf(this->floor_db_ * 2.0f) / 2.0f);
  }
  // else quiet: don't publish at all.
}

void AudioSentinel::dump_config() {
  ESP_LOGCONFIG(TAG, "Audio Sentinel:");
  ESP_LOGCONFIG(TAG, "  live interval: %u ms, events interval: %u ms", (unsigned) this->live_interval_,
                (unsigned) this->events_interval_);
  ESP_LOGCONFIG(TAG, "  initial floor: %.1f dB, drift: %.4f dB/tick, margin: %.1f dB", this->initial_floor_db_,
                this->floor_drift_db_, this->margin_db_);
  ESP_LOGCONFIG(TAG, "  hysteresis: %.1f dB, hold: %u ms, glide: %.2f, attack: %.1f dB", this->hysteresis_db_,
                (unsigned) this->hold_ms_, this->glide_, this->attack_db_);
  ESP_LOGCONFIG(TAG, "  ring buffer: %d samples, /api/audio_buffer %s", RING_SIZE,
                this->web_server_base_ != nullptr ? "enabled" : "disabled");
}

}  // namespace audio_sentinel
}  // namespace esphome
