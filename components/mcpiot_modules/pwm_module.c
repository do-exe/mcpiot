#include "pwm_module.h"
#include "mcpiot_rpc.h"
#include "mcpiot_hal.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "pwm_module";

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

static char *make_error(int id, int code, const char *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(root, "error", err);
    cJSON_AddNumberToObject(root, "id", id);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/* ── RPC handler ───────────────────────────────────────────────────────── */

/*
 * pwm.set  {"pin": N, "duty": 0-100, "freq": Hz}
 *           duty = 0   → off
 *           duty = 100 → full on
 *           freq = tone frequency (e.g. 1000 for 1kHz buzzer tone)
 *
 * Returns: {"ok": true, "pin": N, "duty": 50.0, "freq": 1000}
 */
static char *handle_pwm_set(cJSON *params, int id)
{
    cJSON *pin_item = params ? cJSON_GetObjectItem(params, "pin") : NULL;
    cJSON *duty_item = params ? cJSON_GetObjectItem(params, "duty") : NULL;
    cJSON *freq_item = params ? cJSON_GetObjectItem(params, "freq") : NULL;

    if (!pin_item || !cJSON_IsNumber(pin_item) ||
        !duty_item || !cJSON_IsNumber(duty_item) ||
        !freq_item || !cJSON_IsNumber(freq_item))
        return make_error(id, -32602, "pin, duty (0-100), freq (Hz) required");

    int pin = (int)pin_item->valuedouble;
    float duty = (float)duty_item->valuedouble;
    int freq = (int)freq_item->valuedouble;

    if (duty < 0.0f || duty > 100.0f)
        return make_error(id, -32602, "duty must be 0-100");
    if (freq <= 0)
        return make_error(id, -32602, "freq must be > 0");

    esp_err_t ret = mcpiot_hal_pwm_set(pin, duty, freq);
    if (ret != ESP_OK)
    {
        return make_error(id, -32603, "PWM HAL error");
    }

    ESP_LOGI(TAG, "pwm.set pin=%d duty=%.1f%% freq=%dHz", pin, duty, freq);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "pin", pin);
    cJSON_AddNumberToObject(result, "duty", duty);
    cJSON_AddNumberToObject(result, "freq", freq);
    return make_result(id, result);
}

/* ── Module wiring ─────────────────────────────────────────────────────── */

static esp_err_t pwm_init(int pin)
{
    (void)pin; /* channels configured on first pwm.set call */
    return ESP_OK;
}

static const char *s_methods[] = {"pwm.set"};

static void pwm_register_methods(void)
{
    mcpiot_rpc_register("pwm.set", handle_pwm_set);
    ESP_LOGI(TAG, "registered pwm.set");
}

static void pwm_register_events(void)
{
    /* no events for basic PWM — future: pwm.cycle_complete */
}

const mcpiot_module_driver_t pwm_module_driver = {
    .type = "pwm",
    .pin = -1,
    .init = pwm_init,
    .register_methods = pwm_register_methods,
    .register_events = pwm_register_events,
    .methods = s_methods,
    .method_count = 1,
    .events = NULL,
    .event_count = 0,
};
