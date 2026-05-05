#include "mcpiot.h"
#include "mcpiot_registry.h"
#include "mcpiot_rpc.h"
#include "mcpiot_transport.h"
#include "esp_log.h"

static const char *TAG = "mcpiot";

void mcpiot_init(const mcpiot_config_t *config)
{
    ESP_LOGI(TAG, "MCP-IoT %s | board: %s", MCPIOT_VERSION, config->board);

    mcpiot_registry_init(config->board, config->version);
    mcpiot_rpc_init();
    mcpiot_transport_init(&config->transport);
}

void mcpiot_start(void)
{
    mcpiot_transport_start();
    ESP_LOGI(TAG, "Ready — JSON-RPC 2.0 listening");
}
