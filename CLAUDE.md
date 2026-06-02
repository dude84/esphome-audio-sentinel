# CLAUDE.md — Audio Sentinel

ESPHome firmware: ESP32-S3 + INMP441 mic → Home Assistant audio sentinel (baby
monitor). On-device dB DSP only; no audio recording/streaming. See `README.md`
for hardware + HA setup.

## Architecture

Two ESPHome idioms carry this project — know both before editing:

1. **Packages** (https://esphome.io/components/packages/) — `audio-sentinel.yaml`
   is a thin entrypoint (substitutions + `external_components` + `packages:`).
   Real config is split across `audio-sentinel/packages/*.yaml`, merged non-destructively
   (lists concat / merge by id; main-config substitutions win).
2. **External component** `audio-sentinel/components/audio_sentinel/` — all signal processing.
   This replaced the removed `includes:`/custom-component style. **Do not
   reintroduce `includes:` or inline DSP lambdas.**

Data flow: `INMP441 → i2s_audio → sound_level (spl_db, spl_peak_db) → audio_sentinel → HA`.

## Component internals (`audio-sentinel/components/audio_sentinel/`)

- `__init__.py` — hub `CONFIG_SCHEMA` + `to_code`. Wires source sensors
  (`rms_sensor`/`peak_sensor`), `web_server_base`, threshold `number`s, and tuning
  knobs. Exports `AudioSentinel` + `CONF_AUDIO_SENTINEL_ID` for the platform files.
- `sensor.py` / `binary_sensor.py` — `platform: audio_sentinel` entries. Each named
  sub-key (`peak_live`, `noise_floor`, `peak_events`, `squawk`, …) registers an
  entity and calls a `set_*` setter on the hub. Filters (median/delta/throttle) are
  declared in YAML on these sub-sensors, not in C++.
- `audio_sentinel.h/.cpp` — the DSP. Two timers via `set_interval`:
  - `live_tick_` (every `live_interval`, 250 ms): envelope → adaptive floor → gate →
    ring push + publish `db_live`/`peak_live`/`noise_floor`/threshold mirrors/`est_db`,
    plus squawk/cry hysteresis.
  - `events_tick_` (every `events_interval`, 2 s): peak-hold envelope → `peak_events`.
  - Ring buffer (`RING_SIZE=1200`) + `AudioBufferHandler` registered on
    `web_server_base` in `setup()`, serving `GET /api/audio_buffer?count=N`.
  - All former lambda function-`static`s are now instance members (`env_`, `floor_db_`,
    `ev_s_`, `ev_t_peak_`, latches, …).

### Tuning knobs (`audio_sentinel:` in `audio-sentinel/packages/sentinel.yaml`)
`initial_floor_db`, `floor_drift_db`, `floor_alpha`, `margin_db` (noise floor) ·
`attack_coeff`/`release_coeff` (live envelope — `attack_coeff` 1.0 = legacy instant
attack, <1.0 smooths the live trace; alarms use raw peak so latency is unaffected) ·
`hold`/`glide`/`attack_db`/`events_input_coeff` (events peak-hold — variant C ships
`hold 4s`, `glide 0.30`, `events_input_coeff 0.75` to track peaks and decay fast) ·
`hysteresis_db` (squawk/cry) · `live_interval`/`events_interval` (cadence).

## Contracts — don't break these

- **`/api/audio_buffer` JSON shape** `{count, ms, p[], n[]}` is consumed verbatim by
  the `data_generator` in `ha/dashboard.yaml` (it derives timestamps from `count`×`ms`).
  Change the handler and the chart pre-fill breaks.
- **Entity slugs** (`sensor.audio_sentinel_peak_db_live`, `_noise_floor`,
  `_peak_db_events`, `_squawk_threshold`, `_cry_threshold`, `binary_sensor.*`) are
  referenced by the dashboard. Keep the `name:` strings (`${name} …`) stable.
- **`name: audio-sentinel`** substitution drives every slug above.
- HA `rest_command.baby_sentinel_audio_buffer_fetch` (`ha/rest_command.yaml`) must
  point at the device IP / `${static_ip}`.

## Commands

```bash
esphome config  audio-sentinel.yaml   # validate (schema + package merge)
esphome compile audio-sentinel.yaml   # compile external component
esphome run     audio-sentinel.yaml   # flash (USB then OTA)
```

## Gotchas

- `secrets.yaml` is git-ignored; only `secrets.yaml.example` is committed. Local
  `!include` packages may use `!secret`; *remote git* packages may not.
- The audio gate (`switch.audio_sentinel_audio`, OTA pause) is a plain bool on the
  hub (`set_audio_enabled`/`is_audio_enabled`), defaulting ON each boot — no
  restore global. The small switch/OTA lambdas are normal automation lambdas
  (allowed); they are not the removed custom-component pattern.
- `CODEOWNERS = ["@dude84"]` in `__init__.py`.
- Heap is reported via the official `debug` component, not raw `esp_get_*` lambdas.
