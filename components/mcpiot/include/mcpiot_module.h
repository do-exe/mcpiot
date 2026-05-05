#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Module driver contract.
     *
     * Every hardware module (GPIO, PIR, buzzer, LED, camera, …) implements
     * this struct and exposes it as a global const.  The mcpiot Studio
     * generator adds a pointer to each driver into the s_modules[] array
     * in main.c — the core code never changes, only that list grows.
     *
     * Adding a new module type = create one .c + .h in components/mcpiot_modules/
     * and append &my_module_driver to s_modules[] in main.c.
     */
    typedef struct
    {
        const char *type; /**< "gpio", "pir_sensor", "buzzer", …       */
        int pin;          /**< primary GPIO pin, -1 if not applicable   */

        esp_err_t (*init)(int pin);     /**< one-time hardware initialisation        */
        void (*register_methods)(void); /**< calls mcpiot_rpc_register() per method  */
        void (*register_events)(void);  /**< calls mcpiot_event_register() per event */

        const char **methods; /**< method name list (goes into registry)    */
        int method_count;

        const char **events; /**< event name list  (goes into registry)    */
        int event_count;
    } mcpiot_module_driver_t;

#ifdef __cplusplus
}
#endif
