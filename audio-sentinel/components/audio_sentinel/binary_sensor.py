import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_SOUND

from . import CONF_AUDIO_SENTINEL_ID, AudioSentinel

DEPENDENCIES = ["audio_sentinel"]

CONF_SQUAWK = "squawk"
CONF_CRY = "cry"

# Sub-binary-sensor key -> C++ setter name on the hub.
_BINARY_SENSORS = {
    CONF_SQUAWK: "set_squawk_binary_sensor",
    CONF_CRY: "set_cry_binary_sensor",
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_AUDIO_SENTINEL_ID): cv.use_id(AudioSentinel),
        **{
            cv.Optional(key): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_SOUND
            )
            for key in _BINARY_SENSORS
        },
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_AUDIO_SENTINEL_ID])
    for key, setter in _BINARY_SENSORS.items():
        if key in config:
            bs = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(hub, setter)(bs))
