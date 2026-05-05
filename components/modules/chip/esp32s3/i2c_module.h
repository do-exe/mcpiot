#pragma once

#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * I2C module driver.
     * Exposes i2c.scan, i2c.read_reg, i2c.write_reg over JSON-RPC.
     *
     * SDA/SCL pins are specified per-call — no fixed pin assignment.
     * Any free GPIO pair works (avoid strapping/flash/PSRAM pins).
     * Common choices: SDA=8 SCL=9  or  SDA=21 SCL=22.
     *
     * Add to project.json:
     *   "chip/esp32s3/i2c_module"
     */
    extern const mcpiot_module_driver_t i2c_module_driver;

#ifdef __cplusplus
}
#endif
