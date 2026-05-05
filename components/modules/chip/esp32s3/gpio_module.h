#pragma once

#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * GPIO module driver.
     * Exposes gpio.read and gpio.write over JSON-RPC.
     * Uses mcpiot_hal — no direct ESP-IDF GPIO calls here.
     *
     * Add to main.c:
     *   #include "gpio_module.h"
     *   &gpio_module_driver   ← in s_modules[]
     */
    extern const mcpiot_module_driver_t gpio_module_driver;

#ifdef __cplusplus
}
#endif
