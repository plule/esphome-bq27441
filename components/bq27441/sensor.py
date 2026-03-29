import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_VOLTAGE,
    CONF_BATTERY_LEVEL,
    CONF_CAPACITY,
    CONF_CURRENT,
    CONF_ENERGY,
    CONF_POWER,
    CONF_TEMPERATURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_POWER,
    ICON_HEART_PULSE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_MILLIVOLT,
    UNIT_CELSIUS,
    UNIT_WATT,
    UNIT_MILLIAMP,
)
from esphome.components import i2c

CONF_BQ27441_ID = "bq27441_id"
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["sensor"]

CONF_REMAINING_CAPACITY = "remaining_capacity"
CONF_BATTERY_HEALTH = "battery_health"

ns = cg.esphome_ns.namespace("bq27441")
BQ27441 = ns.class_("BQ27441", cg.PollingComponent, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BQ27441),
            cv.Optional(CONF_CAPACITY): cv.positive_int,
            cv.Optional(CONF_ENERGY): cv.positive_int,
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLIVOLT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_REMAINING_CAPACITY): sensor.sensor_schema(
                unit_of_measurement="mAh",
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLIAMP,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_BATTERY_HEALTH): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_HEART_PULSE,
                accuracy_decimals=0,
                device_class="",
                state_class=STATE_CLASS_MEASUREMENT,
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x55))
)

SENSOR_MAP = {
    CONF_BATTERY_LEVEL: "set_level_sensor",
    CONF_BATTERY_VOLTAGE: "set_voltage_sensor",
    CONF_REMAINING_CAPACITY: "set_remaining_capacity_sensor",
    CONF_TEMPERATURE: "set_temperature_sensor",
    CONF_POWER: "set_power_sensor",
    CONF_BATTERY_HEALTH: "set_health_sensor",
    CONF_CURRENT: "set_current_sensor",
}


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    if conf := config.get(CONF_CAPACITY):
        cg.add(var.set_capacity(conf))
    if conf := config.get(CONF_ENERGY):
        cg.add(var.set_energy(conf))

    for key, func_name in SENSOR_MAP.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(var, func_name)(sens))

    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
