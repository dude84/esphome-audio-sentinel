import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number, sensor, web_server_base
from esphome.const import CONF_ID

CODEOWNERS = ["@dude84"]
DEPENDENCIES = ["web_server_base"]
AUTO_LOAD = ["sensor", "binary_sensor"]

audio_sentinel_ns = cg.esphome_ns.namespace("audio_sentinel")
AudioSentinel = audio_sentinel_ns.class_("AudioSentinel", cg.Component)

CONF_AUDIO_SENTINEL_ID = "audio_sentinel_id"

CONF_WEB_SERVER_BASE_ID = "web_server_base_id"
CONF_RMS_SENSOR = "rms_sensor"
CONF_PEAK_SENSOR = "peak_sensor"
CONF_SQUAWK_NUMBER = "squawk_number"
CONF_CRY_NUMBER = "cry_number"
CONF_OFFSET_NUMBER = "offset_number"

CONF_LIVE_INTERVAL = "live_interval"
CONF_EVENTS_INTERVAL = "events_interval"
CONF_INITIAL_FLOOR_DB = "initial_floor_db"
CONF_FLOOR_DRIFT_DB = "floor_drift_db"
CONF_MARGIN_DB = "margin_db"
CONF_HYSTERESIS_DB = "hysteresis_db"
CONF_FLOOR_ALPHA = "floor_alpha"
CONF_RELEASE_COEFF = "release_coeff"
CONF_ATTACK_COEFF = "attack_coeff"
CONF_HOLD = "hold"
CONF_GLIDE = "glide"
CONF_ATTACK_DB = "attack_db"
CONF_EVENTS_INPUT_COEFF = "events_input_coeff"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AudioSentinel),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
            web_server_base.WebServerBase
        ),
        cv.Required(CONF_RMS_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PEAK_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_SQUAWK_NUMBER): cv.use_id(number.Number),
        cv.Optional(CONF_CRY_NUMBER): cv.use_id(number.Number),
        cv.Optional(CONF_OFFSET_NUMBER): cv.use_id(number.Number),
        cv.Optional(CONF_LIVE_INTERVAL, default="250ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_EVENTS_INTERVAL, default="2s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INITIAL_FLOOR_DB, default=-60.0): cv.float_,
        cv.Optional(CONF_FLOOR_DRIFT_DB, default=0.001): cv.float_,
        cv.Optional(CONF_MARGIN_DB, default=2.0): cv.float_,
        cv.Optional(CONF_HYSTERESIS_DB, default=1.5): cv.float_,
        cv.Optional(CONF_FLOOR_ALPHA, default=0.005): cv.float_,
        cv.Optional(CONF_RELEASE_COEFF, default=0.15): cv.float_,
        cv.Optional(CONF_ATTACK_COEFF, default=1.0): cv.float_,
        cv.Optional(CONF_HOLD, default="20s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_GLIDE, default=0.10): cv.float_,
        cv.Optional(CONF_ATTACK_DB, default=0.5): cv.float_,
        cv.Optional(CONF_EVENTS_INPUT_COEFF, default=0.40): cv.float_,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    wsb = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_server_base(wsb))

    rms = await cg.get_variable(config[CONF_RMS_SENSOR])
    cg.add(var.set_rms_sensor(rms))
    peak = await cg.get_variable(config[CONF_PEAK_SENSOR])
    cg.add(var.set_peak_sensor(peak))

    for key, setter in (
        (CONF_SQUAWK_NUMBER, var.set_squawk_number),
        (CONF_CRY_NUMBER, var.set_cry_number),
        (CONF_OFFSET_NUMBER, var.set_offset_number),
    ):
        if key in config:
            num = await cg.get_variable(config[key])
            cg.add(setter(num))

    cg.add(var.set_live_interval(config[CONF_LIVE_INTERVAL].total_milliseconds))
    cg.add(var.set_events_interval(config[CONF_EVENTS_INTERVAL].total_milliseconds))
    cg.add(var.set_initial_floor_db(config[CONF_INITIAL_FLOOR_DB]))
    cg.add(var.set_floor_drift_db(config[CONF_FLOOR_DRIFT_DB]))
    cg.add(var.set_margin_db(config[CONF_MARGIN_DB]))
    cg.add(var.set_hysteresis_db(config[CONF_HYSTERESIS_DB]))
    cg.add(var.set_floor_alpha(config[CONF_FLOOR_ALPHA]))
    cg.add(var.set_release_coeff(config[CONF_RELEASE_COEFF]))
    cg.add(var.set_attack_coeff(config[CONF_ATTACK_COEFF]))
    cg.add(var.set_hold_ms(config[CONF_HOLD].total_milliseconds))
    cg.add(var.set_glide(config[CONF_GLIDE]))
    cg.add(var.set_attack_db(config[CONF_ATTACK_DB]))
    cg.add(var.set_events_input_coeff(config[CONF_EVENTS_INPUT_COEFF]))
