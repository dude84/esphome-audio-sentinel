# Audio Sentinel

ESPHome firmware for an **ESP32-S3 + INMP441 I²S microphone** that turns the mic
into a low-latency **audio sentinel** — built for monitoring a sleeping baby (or
any person), surfaced in **Home Assistant** as live + historical sound charts and
two configurable alarms (*squawk* and *cry*).

It does **not** record or stream audio. It only computes sound-level (dB) features
on-device and publishes numbers, so it is privacy-preserving by design.

---

## What it does

The INMP441 feeds the official ESPHome `sound_level` sensor (RMS + peak dB). A
custom **`audio_sentinel` external component** then runs the signal processing:

- **Adaptive noise floor** — a gated EMA that learns the room's ambient level and
  drifts slowly, so the baseline tracks a quiet nursery without chasing events.
- **Smoothed envelope** (instant attack / moderate release) so a cry shows up
  immediately but doesn't flicker.
- **Two visualization series**:
  - **Live (short window)** — 5-minute peak + noise-floor trace, gated so quiet
    periods read as a flat line and events stand out.
  - **Events (long window)** — a peak-hold envelope (20 s hold, glide-to-baseline)
    recorded with HA long-term statistics for a 4-hour overview.
- **Squawk / Cry binary sensors** — hysteresis comparators on peak dB against two
  user-adjustable thresholds. Wire these to HA automations/notifications so a
  caregiver is alerted when the baby is stirring vs. genuinely crying.
- **Ring buffer + HTTP endpoint** (`GET /api/audio_buffer`) so the HA chart can
  back-fill the last 5 minutes the instant the dashboard loads.

The Home Assistant view (ApexCharts) shows a **short window** (live, 5 min) and a
**long window** (events, 4 h), with the squawk/cry thresholds drawn as guide lines.

---

## Hardware

| Signal        | ESP32-S3 pin | INMP441 pin |
|---------------|--------------|-------------|
| I²S BCLK      | GPIO10       | SCK         |
| I²S LRCLK/WS  | GPIO11       | WS          |
| I²S DIN       | GPIO04       | SD          |
| Status LED    | GPIO21       | — (onboard WS2812) |
| 3V3 / GND     | 3V3 / GND    | VDD / GND, **L/R → 3V3** |

- Board: `esp32-s3-devkitc-1` (Arduino framework).
- INMP441 `L/R` tied high → right channel (matches `channel: right` in `mic.yaml`).
- Onboard WS2812 status LED: off at boot, **blue** once Wi-Fi connects.

---

## Repository layout

```
audio-sentinel.yaml          # thin entrypoint: substitutions + external_components + packages
packages/
  network.yaml               # wifi, api, ota, captive_portal, web_server
  mic.yaml                   # i2s_audio, microphone, sound_level (spl_db / spl_peak_db)
  sentinel.yaml              # audio_sentinel hub + its sensors/binary_sensors + thresholds + audio switch
  diagnostics.yaml           # heap (debug), uptime, status LED
components/
  audio_sentinel/            # the external component (Python codegen + C++ DSP)
    __init__.py  sensor.py  binary_sensor.py  audio_sentinel.h  audio_sentinel.cpp
ha/
  dashboard.yaml             # ApexCharts card (short + long window)
  rest_command.yaml          # HA rest_command that calls /api/audio_buffer
secrets.yaml.example         # template — copy to secrets.yaml (git-ignored)
```

The firmware uses the ESPHome [packages](https://esphome.io/components/packages/)
pattern (the top file just `!include`s the packages) and a local
[external component](https://developers.esphome.io/blog/2025/02/19/about-the-removal-of-support-for-custom-components/)
(the sanctioned replacement for the now-removed `includes:`/custom-component style).

---

## Flashing

1. Install ESPHome (`pip install esphome` or the HA add-on).
2. `cp secrets.yaml.example secrets.yaml` and fill in your values.
3. Adjust `substitutions:` in `audio-sentinel.yaml` (pins, `static_ip`).
4. Validate, then flash:

   ```bash
   esphome config  audio-sentinel.yaml     # validate config + package merge
   esphome compile audio-sentinel.yaml     # compile (builds the external component)
   esphome run     audio-sentinel.yaml     # flash (USB first time, OTA after)
   ```

Verify the endpoint after boot:

```bash
curl "http://<device-ip>/api/audio_buffer?count=1200"
# -> {"count":1200,"ms":250,"p":[...],"n":[...]}
```

---

## Home Assistant setup

1. Install the [ApexCharts card](https://github.com/RomRider/apexcharts-card)
   (and [card-mod](https://github.com/thomasloven/lovelace-card-mod) for the
   spinner-hiding style) via HACS.
2. Add the `rest_command` from `ha/rest_command.yaml` to `configuration.yaml`
   (set the host to your device IP), and restart HA. The card's `data_generator`
   calls `rest_command.baby_sentinel_audio_buffer_fetch` to pre-fill history.
3. Add `ha/dashboard.yaml` as a manual/dashboard card.

Entities published (slugs assume the default `name: audio-sentinel`):

| Entity | Purpose |
|--------|---------|
| `sensor.audio_sentinel_peak_db_live` | live peak (short window) |
| `sensor.audio_sentinel_noise_floor` | adaptive noise floor |
| `sensor.audio_sentinel_peak_db_events` | peak-hold events (long window, statistics) |
| `sensor.audio_sentinel_squawk_threshold` / `_cry_threshold` | threshold guide lines |
| `binary_sensor.audio_sentinel_squawk` / `_cry` | alarm triggers (device_class: sound) |
| `number.audio_sentinel_squawk_threshold_db` / `_cry_threshold_db` | adjust thresholds |
| `switch.audio_sentinel_audio` | pause/resume processing |

---

## Tuning

Set thresholds live from HA (the `number.*` entities) while watching the live
chart: raise **Squawk** until normal sleep noises stay below it; set **Cry** near
the level of a real cry. DSP behavior (floor drift, hold/glide, margins) is tunable
via the `audio_sentinel:` keys in `packages/sentinel.yaml` — see `CLAUDE.md` for
the full knob list and what each does.
