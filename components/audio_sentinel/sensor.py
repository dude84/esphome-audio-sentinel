import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_SOUND_PRESSURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL,
)

from . import CONF_AUDIO_SENTINEL_ID, AudioSentinel

DEPENDENCIES = ["audio_sentinel"]

CONF_DB_LIVE = "db_live"
CONF_PEAK_LIVE = "peak_live"
CONF_PEAK_EVENTS = "peak_events"
CONF_NOISE_FLOOR = "noise_floor"
CONF_SQUAWK_THRESHOLD = "squawk_threshold"
CONF_CRY_THRESHOLD = "cry_threshold"
CONF_EST_DB = "est_db"

# Sub-sensor key -> C++ setter name on the hub.
_SENSORS = {
    CONF_DB_LIVE: "set_db_live_sensor",
    CONF_PEAK_LIVE: "set_peak_live_sensor",
    CONF_PEAK_EVENTS: "set_peak_events_sensor",
    CONF_NOISE_FLOOR: "set_noise_floor_sensor",
    CONF_SQUAWK_THRESHOLD: "set_squawk_threshold_sensor",
    CONF_CRY_THRESHOLD: "set_cry_threshold_sensor",
    CONF_EST_DB: "set_est_db_sensor",
}


def _sub_sensor_schema():
    return sensor.sensor_schema(
        unit_of_measurement=UNIT_DECIBEL,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_SOUND_PRESSURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AUDIO_SENTINEL_ID): cv.use_id(AudioSentinel),
        **{cv.Optional(key): _sub_sensor_schema() for key in _SENSORS},
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_AUDIO_SENTINEL_ID])
    for key, setter in _SENSORS.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(hub, setter)(sens))
