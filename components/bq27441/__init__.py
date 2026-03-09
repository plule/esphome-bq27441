import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_CAPACITY,
    CONF_ENERGY,
)
from esphome.components import i2c

CONF_BQ27441_ID = "bq27441_id"
DEPENDENCIES = ["i2c"]

ns = cg.esphome_ns.namespace("bq27441")
BQ27441 = ns.class_("BQ27441", cg.PollingComponent, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BQ27441),
            cv.Optional(CONF_CAPACITY): cv.positive_int,
            cv.Optional(CONF_ENERGY): cv.positive_int,
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x55))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    if conf := config.get(CONF_CAPACITY):
        cg.add(var.set_capacity(conf))
    if conf := config.get(CONF_ENERGY):
        cg.add(var.set_energy(conf))

    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
