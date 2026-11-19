import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]

CONF_PA_PIN = "pa_pin"
CONF_LNA_PIN = "lna_pin"
CONF_PA_TABLE = "pa_table"

velua_ns = cg.esphome_ns.namespace("velua_remote")
VeluaRemote = velua_ns.class_("VeluaRemote", cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(VeluaRemote),
            cv.Required(CONF_PA_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_LNA_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_PA_TABLE, default=0x60): cv.hex_uint8_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    pa = await cg.gpio_pin_expression(config[CONF_PA_PIN])
    cg.add(var.set_pa_pin(pa))
    lna = await cg.gpio_pin_expression(config[CONF_LNA_PIN])
    cg.add(var.set_lna_pin(lna))
    cg.add(var.set_pa_table(config[CONF_PA_TABLE]))
