#include "adc_module.h"
#include "mcpiot_rpc.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "adc_module";

/* ── Internal state ────────────────────────────────────────────────────── */

static adc_oneshot_unit_handle_t s_adc1_handle = NULL;

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

/* ── RPC: adc.read ─────────────────────────────────────────────────────── */

/*
 * adc.read  {"pin": N}
 *
 * Reads the voltage on an ADC-capable GPIO pin.
 * ADC1 only (GPIO 1-10 on ESP32-S3). ADC2 is unusable while Wi-Fi is active.
 *
 * Returns: {"pin": N, "channel": N, "raw": 0-4095, "mv": N}
 *   mv = -1 means calibration data not available; raw value still valid.
 */
static char *handle_adc_read(cJSON *params, int id)
{
    cJSON *pin_item = params ? cJSON_GetObjectItem(params, "pin") : NULL;
    if (!pin_item || !cJSON_IsNumber(pin_item))
        return make_error(id, -32602, "pin required");

    int pin = (int)pin_item->valuedouble;

    /* Map GPIO → ADC unit + channel */
    adc_unit_t unit;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(pin, &unit, &channel) != ESP_OK)
        return make_error(id, -32602, "pin is not an ADC-capable GPIO");

    if (unit != ADC_UNIT_1)
        return make_error(id, -32602, "only ADC1 (GPIO 1-10) supported; ADC2 conflicts with Wi-Fi");

    if (s_adc1_handle == NULL)
        return make_error(id, -32000, "ADC not initialised");

    /* Configure the channel (idempotent — safe to call multiple times) */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,  /* 12-bit on ESP32-S3 */
        .atten    = ADC_ATTEN_DB_12,       /* 0–3100 mV range     */
    };
    if (adc_oneshot_config_channel(s_adc1_handle, channel, &chan_cfg) != ESP_OK)
        return make_error(id, -32000, "ADC channel config failed");

    /* Read raw value */
    int raw = 0;
    if (adc_oneshot_read(s_adc1_handle, channel, &raw) != ESP_OK)
        return make_error(id, -32000, "ADC read failed");

    /* Try curve-fitting calibration (ESP32-S3 supports it) */
    int mv = -1;
    adc_cali_handle_t cali_handle = NULL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = channel,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle) == ESP_OK)
    {
        adc_cali_raw_to_voltage(cali_handle, raw, &mv);
        adc_cali_delete_scheme_curve_fitting(cali_handle);
    }
#endif

    ESP_LOGI(TAG, "adc.read pin=%d ch=%d raw=%d mv=%d", pin, (int)channel, raw, mv);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "pin",     pin);
    cJSON_AddNumberToObject(result, "channel", (int)channel);
    cJSON_AddNumberToObject(result, "raw",     raw);
    cJSON_AddNumberToObject(result, "mv",      mv);
    return make_result(id, result);
}

/* ── Module wiring ─────────────────────────────────────────────────────── */

static esp_err_t adc_init(int pin)
{
    (void)pin;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &s_adc1_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ADC1 unit init failed: %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "ADC1 unit ready");
    return ESP_OK;
}

static const char *s_methods[] = {"adc.read"};

static void adc_register_methods(void)
{
    mcpiot_rpc_register("adc.read", handle_adc_read);
    ESP_LOGI(TAG, "registered adc.read");
}

static void adc_register_events(void) {}

const mcpiot_module_driver_t adc_module_driver = {
    .type             = "adc",
    .pin              = -1,
    .init             = adc_init,
    .register_methods = adc_register_methods,
    .register_events  = adc_register_events,
    .methods          = s_methods,
    .method_count     = 1,
    .events           = NULL,
    .event_count      = 0,
};
