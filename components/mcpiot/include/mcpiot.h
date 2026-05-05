#pragma once

#include "mcpiot_registry.h"
#include "mcpiot_transport.h"
#include "mcpiot_module.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MCPIOT_VERSION "1.0.0"

    typedef struct
    {
        const char *board;   /**< e.g. "esp32s3"        */
        const char *version; /**< firmware version      */
        mcpiot_transport_config_t transport;
    } mcpiot_config_t;

    /** Initialise registry, RPC dispatcher, and transport. Call before load_modules. */
    void mcpiot_init(const mcpiot_config_t *config);

    /**
     * Load module drivers — calls init(), register_methods(), register_events()
     * on each driver and records it in the capability registry.
     *
     * @param modules  array of pointers to module driver structs
     * @param count    number of entries
     *
     * Generated main.c usage:
     *   static const mcpiot_module_driver_t * const s_modules[] = {
     *       &gpio_module_driver,
     *       &pir_module_driver,   // future
     *   };
     *   mcpiot_load_modules(s_modules, sizeof(s_modules)/sizeof(s_modules[0]));
     */
    void mcpiot_load_modules(const mcpiot_module_driver_t *const *modules, int count);

    /** Start the RPC listener task. Call after mcpiot_load_modules(). */
    void mcpiot_start(void);

#ifdef __cplusplus
}
#endif
