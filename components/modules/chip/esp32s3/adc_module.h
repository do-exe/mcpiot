#pragma once

#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * ADC module driver.
     * Exposes adc.read over JSON-RPC.
     *
     * Supports ADC1 channels only (GPIO 1-10 on ESP32-S3).
     * ADC2 is disabled while Wi-Fi is active — do NOT use GPIO 11-20.
     *
     * Add to project.json:
     *   "chip/esp32s3/adc_module"
     */
    extern const mcpiot_module_driver_t adc_module_driver;

#ifdef __cplusplus
}
#endif
