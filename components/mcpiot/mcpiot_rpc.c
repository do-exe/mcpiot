#include "mcpiot_rpc.h"
#include "mcpiot_registry.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "mcpiot_rpc";

/* ── Method table ────────────────────────────────────────────────────── */

typedef struct {
    char             method[64];
    mcpiot_handler_t handler;
} mcpiot_method_entry_t;

static mcpiot_method_entry_t s_methods[MCPIOT_MAX_RPC_METHODS];
static int                    s_method_count = 0;

/* ── Response helpers ────────────────────────────────────────────────── */

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

static char *make_result(int id, cJSON *result)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddItemToObject(root, "result", result);  /* result ownership transferred */
    cJSON_AddNumberToObject(root, "id", id);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/* ── Built-in handlers ───────────────────────────────────────────────── */

/* mcpiot.info — board identity */
static char *handle_mcpiot_info(cJSON *params, int id)
{
    const mcpiot_registry_t *reg = mcpiot_registry_get();
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "board",    reg->board);
    cJSON_AddStringToObject(result, "version",  reg->version);
    cJSON_AddStringToObject(result, "protocol", "mcpiot/1.0");
    return make_result(id, result);
}

/* mcpiot.capabilities — full self-describing manifest */
static char *handle_mcpiot_capabilities(cJSON *params, int id)
{
    char  *cap_str = mcpiot_registry_to_json();
    cJSON *result  = cJSON_Parse(cap_str);
    free(cap_str);
    if (!result) result = cJSON_CreateObject();
    return make_result(id, result);
}

/* gpio.read — read a digital pin level */
static char *handle_gpio_read(cJSON *params, int id)
{
    cJSON *pin_item = params ? cJSON_GetObjectItem(params, "pin") : NULL;
    if (!pin_item || !cJSON_IsNumber(pin_item)) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "ok", false);
        cJSON_AddStringToObject(r, "error", "missing or invalid pin");
        return make_result(id, r);
    }
    int pin = (int)pin_item->valuedouble;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "pin",   pin);
    cJSON_AddNumberToObject(result, "value", gpio_get_level(pin));
    return make_result(id, result);
}

/* gpio.write — set a digital pin level */
static char *handle_gpio_write(cJSON *params, int id)
{
    cJSON *pin_item = params ? cJSON_GetObjectItem(params, "pin")   : NULL;
    cJSON *val_item = params ? cJSON_GetObjectItem(params, "value") : NULL;

    if (!pin_item || !cJSON_IsNumber(pin_item) ||
        !val_item || !cJSON_IsNumber(val_item)) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddBoolToObject(r, "ok", false);
        cJSON_AddStringToObject(r, "error", "missing or invalid pin or value");
        return make_result(id, r);
    }
    int pin = (int)pin_item->valuedouble;
    int val = (int)val_item->valuedouble;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(pin, val ? 1 : 0);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "pin",   pin);
    cJSON_AddNumberToObject(result, "value", val ? 1 : 0);
    return make_result(id, result);
}

/* ── Public API ──────────────────────────────────────────────────────── */

void mcpiot_rpc_init(void)
{
    s_method_count = 0;
    memset(s_methods, 0, sizeof(s_methods));

    mcpiot_rpc_register("mcpiot.info",         handle_mcpiot_info);
    mcpiot_rpc_register("mcpiot.capabilities", handle_mcpiot_capabilities);
    mcpiot_rpc_register("gpio.read",            handle_gpio_read);
    mcpiot_rpc_register("gpio.write",           handle_gpio_write);

    ESP_LOGI(TAG, "%d built-in methods registered", s_method_count);
}

void mcpiot_rpc_register(const char *method, mcpiot_handler_t handler)
{
    if (s_method_count >= MCPIOT_MAX_RPC_METHODS) {
        ESP_LOGW(TAG, "Method table full — cannot register '%s'", method);
        return;
    }
    strncpy(s_methods[s_method_count].method, method,
            sizeof(s_methods[0].method) - 1);
    s_methods[s_method_count].handler = handler;
    s_method_count++;
}

char *mcpiot_rpc_process(const char *json_str)
{
    cJSON *req = cJSON_Parse(json_str);
    if (!req) {
        return make_error(0, -32700, "parse error");
    }

    cJSON *j_ver    = cJSON_GetObjectItem(req, "jsonrpc");
    cJSON *j_method = cJSON_GetObjectItem(req, "method");
    cJSON *j_id     = cJSON_GetObjectItem(req, "id");
    int    id       = (j_id && cJSON_IsNumber(j_id)) ? (int)j_id->valuedouble : 0;

    if (!j_ver || !cJSON_IsString(j_ver) ||
        strcmp(j_ver->valuestring, "2.0") != 0 ||
        !j_method || !cJSON_IsString(j_method)) {
        cJSON_Delete(req);
        return make_error(id, -32600, "invalid request");
    }

    const char *method = j_method->valuestring;
    cJSON      *params = cJSON_GetObjectItem(req, "params");

    for (int i = 0; i < s_method_count; i++) {
        if (strcmp(s_methods[i].method, method) == 0) {
            char *response = s_methods[i].handler(params, id);
            cJSON_Delete(req);
            return response;
        }
    }

    cJSON_Delete(req);
    return make_error(id, -32601, "method not found");
}
