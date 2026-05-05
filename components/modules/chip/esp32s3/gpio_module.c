#include "gpio_module.h"
#include "mcpiot_rpc.h"
#include "mcpiot_hal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "gpio_module";

/* ── Response helpers ──────────────────────────────────────────────────── */

static char *make_result(int id, cJSON *result)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddItemToObject(root, "result", result);
    cJSON_AddNumberToObject(root, "id", id);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

static char *make_error(int id, int code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    cJSON_AddItemToObject(root, "error", err);
    cJSON_AddNumberToObject(root, "id", id);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/* ── RPC handlers ──────────────────────────────────────────────────────── */

/* gpio.read  {"pin": N}  →  {"ok": true, "pin": N, "value": 0|1} */
static char *handle_gpio_read(cJSON *params, int id)
{
    cJSON *pin_item = params ? cJSON_GetObjectItem(params, "pin") : NULL;
    if (!pin_item || !cJSON_IsNumber(pin_item))
        return make_error(id, -32602, "pin required");

    int pin = (int)pin_item->valuedouble;
    mcpiot_hal_gpio_set_input(pin);
    int level = mcpiot_hal_gpio_read(pin);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "pin", pin);
    cJSON_AddNumberToObject(result, "value", level);
    return make_result(id, result);
}

/* gpio.write  {"pin": N, "value": 0|1}  →  {"ok": true, "pin": N, "value": 0|1} */
static char *handle_gpio_write(cJSON *params, int id)
{
    cJSON *pin_item = params ? cJSON_GetObjectItem(params, "pin") : NULL;
    cJSON *val_item = params ? cJSON_GetObjectItem(params, "value") : NULL;

    if (!pin_item || !cJSON_IsNumber(pin_item) ||
        !val_item || !cJSON_IsNumber(val_item))
        return make_error(id, -32602, "pin and value required");

    int pin = (int)pin_item->valuedouble;
    int val = (int)val_item->valuedouble;
    mcpiot_hal_gpio_set_output(pin);
    mcpiot_hal_gpio_write(pin, val ? 1 : 0);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "pin", pin);
    cJSON_AddNumberToObject(result, "value", val ? 1 : 0);
    return make_result(id, result);
}

/* ── Module wiring ─────────────────────────────────────────────────────── */

static esp_err_t gpio_init(int pin)
{
    (void)pin; /* GPIO direction configured lazily per-request */
    return ESP_OK;
}

static const char *s_methods[] = {"gpio.read", "gpio.write"};

static void gpio_register_methods(void)
{
    mcpiot_rpc_register("gpio.read", handle_gpio_read);
    mcpiot_rpc_register("gpio.write", handle_gpio_write);
    ESP_LOGI(TAG, "registered gpio.read + gpio.write");
}

static void gpio_register_events(void)
{
    /* gpio module has no events — future: gpio.changed interrupt */
}

/* Public driver struct — referenced in main.c s_modules[] */
const mcpiot_module_driver_t gpio_module_driver = {
    .type = "gpio",
    .pin = -1,
    .init = gpio_init,
    .register_methods = gpio_register_methods,
    .register_events = gpio_register_events,
    .methods = s_methods,
    .method_count = 2,
    .events = NULL,
    .event_count = 0,
};
