#pragma once

#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * WiFi module driver — ESP32-S3.
     * Exposes wifi.scan, wifi.connect, wifi.status, wifi.disconnect over JSON-RPC.
     *
     * Add to project.json modules list:
     *   "chip/esp32s3/wifi_module"
     */
    extern const mcpiot_module_driver_t wifi_module_driver;

#ifdef __cplusplus
}
#endif
