# Audio Sentinel

ESPHome firmware for an **ESP32-S3 + INMP441 I²S microphone** that turns the mic
into a low-latency **audio sentinel** — built for monitoring a sleeping baby (or
any person), surfaced in **Home Assistant** as live + historical sound charts and
two configurable alarms (*squawk* and *cry*).

It does **not** record or stream audio. It only computes sound-level (dB) features
on-device and publishes numbers, so it is privacy-preserving by design.

![Home Assistant dashboard: live short-window and 4-hour long-window sound charts with squawk/cry threshold lines, plus Cry and Squawk status cards](_docs/ha_graphs_and_binarysensors.png)

*Home Assistant view — top: live 5-min short window; bottom: 4-hour long window.
**Blue** = peak dB, **grey** = adaptive noise floor, **orange** = squawk threshold,
**red** = cry threshold. The **Cry** / **Squawk** cards below are the binary-sensor
alarms (shown `Clear`).*

---

## What it does

The INMP441 feeds the official ESPHome `sound_level` sensor (RMS + peak dB). The
**`audio_sentinel` external component** does the rest — all on-device, in C++:

- **Two chart series** — a gated **live** trace (5 min) and a peak-hold **events**
  trace (4 h, with HA long-term statistics). Both drawn in ApexCharts against the
  squawk/cry guide lines.
- **Squawk / Cry binary sensors** — hysteresis comparators on peak dB vs. two
  adjustable thresholds; hook them to HA automations to tell *stirring* from a
  *real cry*.
- **Ring buffer + HTTP endpoint** (`GET /api/audio_buffer`) so the chart back-fills
  the last 5 minutes the instant it loads, instead of starting blank.

---

## How the signal processing works

Raw peak dB is noisy and never sits still — even a silent nursery wanders a couple
of dB. The component turns that into two clean, readable traces with four stages.
The figure below runs the **actual DSP** (same coefficients as the firmware) and is
drawn at each panel's **real time scale** — the live chart spans **5 min**, the
events chart spans **4 h** (the dashboard's `graph_span`s). So the same cry is a
wide hump live but a thin spike across 4 h; the shaded sliver shows the entire 5-min
live window is only ~2 % of the long view:

![Audio Sentinel DSP pipeline — top: 5-min live window with raw peak, smoothed envelope, adaptive noise floor and gated output plus squawk/cry thresholds; bottom: 4-hour events window of the peak-hold envelope, with the 5-min live window marked as a ~2% sliver](_docs/filtering.png)

1. **Smoothed envelope** — *fast attack, slow release* (`attack_coeff` = 0.4,
   `release_coeff` = 0.15). A sustained rise ramps to full level in ~1 s — still
   responsive — while single-sample blips are eased instead of spiking the trace,
   and the fall is gentle so it doesn't flicker. (Set `attack_coeff: 1.0` for the
   legacy instant attack.) The **squawk/cry alarms read the *raw* peak, not this
   envelope**, so smoothing the trace never adds alarm latency.
2. **Adaptive noise floor** (grey) — a gated EMA. While the envelope sits within
   `margin_db` (2 dB) of the floor, the floor tracks ambient (`floor_alpha` slow);
   once an event lifts the signal clear, floor tracking is suspended and only a tiny
   upward `floor_drift_db` remains. So the baseline learns the room *without* being
   dragged up by events.
3. **Gate** (blue, *live*) — when the envelope is within `margin_db` of the floor,
   the published value is *snapped to the floor* (quantised to 0.5 dB), so quiet
   periods read as a dead-flat line and real events pop out. Above the margin the
   true peak passes through at 0.1 dB precision.
4. **Peak-hold** (orange, *events*) — a separate envelope tuned to **track the real
   peak**: light input smoothing (`events_input_coeff` = 0.75) so it reaches the true
   level, instant attack, a short **hold** (4 s), then a brisk **glide** down toward a
   ~1-min baseline (`glide` = 0.30). Each event shows as a quick peak that decays
   cleanly rather than a long flat plateau — so a short cry stays visible across the
   4-hour overview without smearing into a hump.

The **squawk/cry binary sensors** compare the *raw* peak against their thresholds
with `hysteresis_db` (1.5 dB): they trip at the threshold but only clear 1.5 dB
below it, so a signal hovering on the line doesn't chatter on/off.

Every knob above is tunable — see [Tuning](#tuning) and `CLAUDE.md`.

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
- Onboard WS2812 RGB LED: **off** (no status automation); the entity stays in HA
  for manual control, and it powers up off each boot.

![Wiring diagram — INMP441 I²S microphone to ESP32-S3 DevKitC-1: VDD and L/R to 3.3V, GND to GND, SCK to GPIO10, WS to GPIO11, SD to GPIO4](_docs/wiring_schema.png)

---

## Repository layout

```
audio-sentinel.yaml          # DEVICE/CONSUMER config: pulls packages + component from github:// (pinned @tag)
audio-sentinel.dev.yaml      # LOCAL-DEV variant: builds packages + component from this working tree
audio-sentinel/              # reusable library, namespaced so it never clashes in a shared dir
  packages/                  #   (consumed remotely by devices, or locally by the .dev entry)
    network.yaml             # wifi, api, ota, captive_portal, web_server  (uses ${substitutions}, no !secret)
    mic.yaml                 # i2s_audio, microphone, sound_level (spl_db / spl_peak_db)
    sentinel.yaml            # audio_sentinel hub + its sensors/binary_sensors + thresholds + audio switch
    diagnostics.yaml         # heap (debug), uptime, status LED
  components/
    audio_sentinel/          # the external component (Python codegen + C++ DSP)
      __init__.py  sensor.py  binary_sensor.py  audio_sentinel.h  audio_sentinel.cpp
ha/
  dashboard.yaml             # ApexCharts card (short + long window)
  rest_command.yaml          # HA rest_command that calls /api/audio_buffer
secrets.yaml.example         # template — copy to secrets.yaml (git-ignored)
```

The firmware uses the ESPHome [packages](https://esphome.io/components/packages/)
pattern + an [external component](https://developers.esphome.io/blog/2025/02/19/about-the-removal-of-support-for-custom-components/)
(the sanctioned replacement for the now-removed `includes:`/custom-component style).
Both are pulled **remotely** from this public repo over `github://`, pinned to a
release tag — so a device is just a small local config that fills in secrets +
wiring. Because remote git packages can't use `!secret`, `network.yaml` takes its
credentials as `${substitutions}` that the device config supplies from `!secret`.

---

## Installation — which file goes where

There are **two independent halves**: the **firmware** (built/flashed by ESPHome)
and the **Home Assistant config** (the chart card + REST command). The `ha/` folder
in this repo is **reference only** — ESPHome never reads it; you copy its contents
into Home Assistant by hand.

### A. Firmware (ESPHome)

A device is a **single local config** that pulls the packages + component from this
repo over `github://` (pinned to a tag) and fills in its own secrets. Nothing under
`audio-sentinel/` needs copying — ESPHome fetches and caches it at build time.

```
/config/esphome/                 # shared ESPHome dir — your other devices live here too
└── audio-sentinel.yaml          # ← the one file you add (pulls everything remotely)
```

1. Add **`audio-sentinel.yaml`** to the dir (copy it from this repo / the
   [v1.1.6 release](https://github.com/dude84/esphome-audio-sentinel/releases)).
   It already points at `github://dude84/esphome-audio-sentinel@v1.1.6`.
2. Create `secrets.yaml` next to it — `wifi_ssid`, `wifi_password`, `failsafe_ap_password`,
   `api_password`, `ota_password` (see `secrets.yaml.example`). The add-on uses
   `/config/esphome/secrets.yaml` for every device.
3. Edit the `substitutions:` at the top of `audio-sentinel.yaml` — `static_ip`,
   GPIO pins, `name`. Secrets are injected from there as `!secret …` substitutions.
4. Build + flash (the **first** build fetches the packages + component from GitHub):
   - **Add-on:** ESPHome dashboard → the device → **Install** (USB first, then OTA).
   - **CLI:** `esphome run audio-sentinel.yaml`
5. After boot it auto-discovers in HA (**Settings → Devices & Services → ESPHome**).
   Sanity-check the buffer endpoint:
   ```bash
   curl "http://<device-ip>/api/audio_buffer?count=1200"
   # -> {"count":1200,"ms":250,"p":[...],"n":[...]}
   ```

**Pinning & multiple devices.** Each device is pinned to a tag (`@v1.1.6`), so a push
to `main` never changes a device until you bump its ref. For several devices, copy
`audio-sentinel.yaml` per device (`nursery.yaml`, `bedroom.yaml`, …) and just change
the `substitutions:` — they all share this one remote library.

**Developing the packages/component.** To change the DSP or packages themselves,
clone the repo and build **`audio-sentinel.dev.yaml`** (includes everything from the
working tree — no fetch), then commit, tag a new release, and bump the device refs.

### B. Home Assistant (chart + REST command)

1. **HACS cards:** install [ApexCharts card](https://github.com/RomRider/apexcharts-card)
   and [card-mod](https://github.com/thomasloven/lovelace-card-mod) (for the
   spinner-hiding style), then reload the browser.
2. **REST command:** copy the `rest_command:` block from `ha/rest_command.yaml` into
   your **`/config/configuration.yaml`** (set the host to your device IP). If you
   already have a `rest_command:` key, merge the entry under it. **Restart HA.**
   This is what the chart calls to back-fill history (see *On-device buffer API*).
3. **Chart card:** in a dashboard choose **Edit → Add card → Manual**, paste the
   contents of `ha/dashboard.yaml`, and save. (Or add it under `cards:` in a
   YAML-mode dashboard.)

> Entity slugs in `ha/dashboard.yaml` assume the default `name: audio-sentinel`.
> If you changed `name:`, update the `entity:` lines (and the URL in
> `ha/rest_command.yaml`) to match.

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

## On-device buffer API — instant chart back-fill

Without this, the short-window chart would start blank and fill one sample every
250 ms — ~5 minutes to look complete. Instead the component keeps a **ring buffer
of the last 1200 samples** (peak dB + noise floor, one pair / 250 ms = 5 min) and
serves the whole window in a single request over the ESPHome web server:

```
GET http://<device-ip>/api/audio_buffer?count=N
```

- `count` — how many of the most recent samples to return (default 480 ≈ 2 min,
  clamped to 1200 ≈ 5 min). Older slots are zero-padded if fewer exist.
- `Access-Control-Allow-Origin: *` is set so the dashboard's JS can fetch it.

When the ApexCharts card loads, its `data_generator` calls this endpoint once (via
`rest_command.baby_sentinel_audio_buffer_fetch`) and seeds both the **Peak** and
**Noise Floor** series with the returned history; from then on it appends live
values. That's the only reason the short-window chart is full immediately.

### Sample payload

Real capture from the device, quiet room (`count=8` for brevity):

```bash
curl "http://<device-ip>/api/audio_buffer?count=8"
```
```json
{"count":8,"ms":250,"p":[-65.0,-65.0,-65.0,-65.0,-65.0,-65.0,-65.0,-65.0],"n":[-64.8,-64.8,-64.8,-64.8,-64.8,-64.8,-64.8,-64.8]}
```

| Field   | Meaning |
|---------|---------|
| `count` | number of samples in this response |
| `ms`    | spacing between samples (250 ms) — the client derives each timestamp as `now - (count - i) * ms`, so no per-sample timestamps are sent |
| `p[]`   | gated peak dB per sample (the live trace) |
| `n[]`   | adaptive noise floor per sample |

A full `count=1200` response is ~10 KB. The arrays are parallel and equal length;
`p[i]` and `n[i]` share the timestamp for index `i`.

---

## Tuning

Set thresholds live from HA (the `number.*` entities) while watching the live
chart: raise **Squawk** until normal sleep noises stay below it; set **Cry** near
the level of a real cry. DSP behavior (floor drift, hold/glide, margins) is tunable
via the `audio_sentinel:` keys in `audio-sentinel/packages/sentinel.yaml` — see
`CLAUDE.md` for the full knob list and what each does.

---

## License

[MIT](LICENSE) © 2026 Maciej Rohleder.

Not affiliated with or endorsed by ESPHome or Home Assistant. This is a sound-level
monitor, **not** a medical or safety device — do not rely on it as the sole means of
monitoring an infant or patient.
