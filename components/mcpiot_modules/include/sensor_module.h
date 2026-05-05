#pragma once

#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Generic sensor module driver (STUB — extend per sensor type).
     *
     * HOW TO ADD A REAL SENSOR:
     * -------------------------
     * 1. Add its type string to the supported list in sensor_module.c
     * 2. Implement its read function: static int read_temperature(int pin) { ... }
     * 3. Add an entry to the s_sensor_readers[] dispatch table
     * 4. If it fires events (e.g. threshold), call mcpiot_event_fire() from an ISR or task
     *
     * RPC call examples:
     *   {"jsonrpc":"2.0","method":"sensor.read","params":{"type":"temperature"},"id":1}
     *   {"jsonrpc":"2.0","method":"sensor.read","params":{"type":"light"},"id":1}
     *   {"jsonrpc":"2.0","method":"sensor.read","params":{"type":"distance"},"id":1}
     *
     * Event pushed to AI (future — after event.subscribe is implemented):
     *   {"jsonrpc":"2.0","method":"event.notify","params":{"event":"sensor.threshold_exceeded","data":{"type":"temperature","value":38}}}
     *
     * Add to main.c s_modules[]:
     *   #include "sensor_module.h"
     *   &sensor_module_driver
     */
    extern const mcpiot_module_driver_t sensor_module_driver;

#ifdef __cplusplus
}
#endif
