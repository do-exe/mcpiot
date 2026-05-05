#pragma once

#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * PWM module driver.
     * Exposes pwm.set over JSON-RPC.
     * Uses LEDC peripheral on ESP32 via mcpiot_hal — no direct ESP-IDF calls here.
     *
     * RPC call:
     *   {"jsonrpc":"2.0","method":"pwm.set","params":{"pin":5,"duty":50,"freq":1000},"id":1}
     *
     * Add to main.c s_modules[]:
     *   #include "pwm_module.h"
     *   &pwm_module_driver
     */
    extern const mcpiot_module_driver_t pwm_module_driver;

#ifdef __cplusplus
}
#endif
