#include "mcpiot.h"
#include "mcpiot_registry.h"
#include "mcpiot_rpc.h"
#include "mcpiot_transport.h"
#include "esp_log.h"

static const char *TAG = "mcpiot";

void mcpiot_init(const mcpiot_config_t *config)
{
    ESP_LOGI(TAG, "mcpiot %s | board: %s", MCPIOT_VERSION, config->board);
    mcpiot_registry_init(config->board, config->version);
    mcpiot_rpc_init();
    mcpiot_transport_init(&config->transport);
}

void mcpiot_load_modules(const mcpiot_module_driver_t *const *modules, int count)
{
    for (int i = 0; i < count; i++)
    {
        const mcpiot_module_driver_t *m = modules[i];
        if (m->init)
            m->init(m->pin);
        if (m->register_methods)
            m->register_methods();
        if (m->register_events)
            m->register_events();
        mcpiot_registry_add_module(m->type, m->pin, m->methods, m->method_count);
        ESP_LOGI(TAG, "loaded: %s (pin=%d, %d methods, %d events)",
                 m->type, m->pin, m->method_count, m->event_count);
    }
}

void mcpiot_start(void)
{
    mcpiot_transport_start();
    ESP_LOGI(TAG, "ready — JSON-RPC 2.0 listening");
}
