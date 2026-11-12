import esphome.codegen as cg
from esphome.components import climate, sensor, uart
import esphome.config_validation as cv

DEPENDENCIES = ["uart", "api"]
AUTO_LOAD = ["sensor"]

xye_ns = cg.esphome_ns.namespace("xye_climate")
XyeClimate = xye_ns.class_("XyeClimate", climate.Climate, cg.Component, uart.UARTDevice)

CONF_POLL_INTERVAL = "poll_interval"
CONF_MIN_SETPOINT_F = "min_setpoint_f"
CONF_MAX_SETPOINT_F = "max_setpoint_f"
CONF_FOLLOW_ME_SENSOR = "follow_me_sensor"
CONF_FOLLOW_ME_INTERVAL = "follow_me_interval"
CONF_FOLLOW_ME_CELSIUS = "follow_me_celsius"
CONF_FOLLOW_ME_SOURCE_F = "follow_me_source_fahrenheit"

CONFIG_SCHEMA = (
    climate.climate_schema(XyeClimate)
    .extend(
        {
            cv.Optional(CONF_POLL_INTERVAL, default="3s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MIN_SETPOINT_F, default=60): cv.int_range(min=40, max=99),
            cv.Optional(CONF_MAX_SETPOINT_F, default=86): cv.int_range(min=40, max=99),
            # Room temperature sensor (any unit; give it in degrees C via a filter
            # if the source is in F). Enables follow-me (C6) when set.
            cv.Optional(CONF_FOLLOW_ME_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_FOLLOW_ME_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
            # Encoding of the C6 temperature byte: whole degrees C (documented)
            # or whole degrees F.
            cv.Optional(CONF_FOLLOW_ME_CELSIUS, default=True): cv.boolean,
            # Unit of the HA entity picked at runtime with set_follow_me_entity().
            cv.Optional(CONF_FOLLOW_ME_SOURCE_F, default=True): cv.boolean,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "xye_climate", baud_rate=4800, require_rx=True, require_tx=True
)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_setpoint_range_f(config[CONF_MIN_SETPOINT_F], config[CONF_MAX_SETPOINT_F]))
    if CONF_FOLLOW_ME_SENSOR in config:
        sens = await cg.get_variable(config[CONF_FOLLOW_ME_SENSOR])
        cg.add(var.set_follow_me_sensor(sens))
    cg.add(var.set_follow_me_interval(config[CONF_FOLLOW_ME_INTERVAL]))
    cg.add(var.set_follow_me_celsius(config[CONF_FOLLOW_ME_CELSIUS]))
    cg.add(var.set_follow_me_source_fahrenheit(config[CONF_FOLLOW_ME_SOURCE_F]))
    # Needed to subscribe to an HA entity chosen at runtime.
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
