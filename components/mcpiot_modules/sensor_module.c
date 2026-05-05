#include "sensor_module.h"
#include "mcpiot_rpc.h"
#include "mcpiot_hal.h"
#include "mcpiot_event.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "sensor_module";

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

/* ── Sensor reader dispatch table ──────────────────────────────────────── */

/*
 * HOW TO ADD A NEW SENSOR TYPE:
 * ------------------------------
 * 1. Write a static read function:
 *      static int read_temperature(void) { ... }   // returns raw value
 *
 * 2. Add it to the table below:
 *      { "temperature", read_temperature },
 *
 * 3. Add its event to s_events[] and call mcpiot_event_register() in
 *    sensor_register_events() for threshold-based push notifications.
 *
 * SENSOR IMPLEMENTATION EXAMPLES:
 *
 *   DHT22 temperature/humidity:
 *     Use ESP-IDF RMT or bit-bang GPIO. Returns temp in °C * 10.
 *     TODO: #include "dht.h" and call dht_read_data(DHT_TYPE_DHT22, pin, &hum, &temp)
 *
 *   LDR light sensor (analog):
 *     Use mcpiot_hal_adc_read(channel) — returns 0-4095 on ESP32 12-bit ADC.
 *     TODO: implement mcpiot_hal_adc_read() in boards/esp32/mcpiot_hal.c first
 *
 *   HC-SR04 ultrasonic distance:
 *     Trigger pulse on TRIG pin, measure echo pulse width on ECHO pin.
 *     TODO: use esp_driver_gptimer or MCPWM capture for accurate timing
 *
 *   PIR motion sensor (digital):
 *     Use mcpiot_hal_gpio_read() — returns 0/1.
 *     Better as a dedicated pir_module with interrupt + event.notify push.
 *     See: components/mcpiot_modules/ — add pir_module.c following gpio_module pattern
 */

typedef struct
{
    const char *type;
    int (*read)(void);
} sensor_reader_t;

/* STUB readers — replace with real driver calls */
static int read_temperature(void)
{
    /* TODO: read from DHT22 / DS18B20 / NTC thermistor / I2C sensor */
    return -1;
}

static int read_light(void)
{
    /* TODO: return mcpiot_hal_adc_read(ADC_CHANNEL_0); — LDR on ADC pin */
    return -1;
}

static int read_distance(void)
{
    /* TODO: HC-SR04 trigger/echo timing — returns distance in cm */
    return -1;
}

static const sensor_reader_t s_readers[] = {
    {"temperature", read_temperature},
    {"light", read_light},
    {"distance", read_distance},
    /* add more here: { "humidity", read_humidity }, */
};
#define READER_COUNT (sizeof(s_readers) / sizeof(s_readers[0]))

/* ── RPC handler ───────────────────────────────────────────────────────── */

/*
 * sensor.read  {"type": "temperature"}
 *              {"type": "light"}
 *              {"type": "distance"}
 *
 * Returns: {"ok": true, "type": "temperature", "value": 24, "unit": "raw"}
 */
static char *handle_sensor_read(cJSON *params, int id)
{
    cJSON *type_item = params ? cJSON_GetObjectItem(params, "type") : NULL;
    if (!type_item || !cJSON_IsString(type_item))
        return make_error(id, -32602, "type required (temperature|light|distance)");

    const char *type = type_item->valuestring;

    for (int i = 0; i < (int)READER_COUNT; i++)
    {
        if (strcmp(s_readers[i].type, type) == 0)
        {
            int value = s_readers[i].read();
            ESP_LOGI(TAG, "sensor.read type=%s value=%d", type, value);

            cJSON *result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "ok", true);
            cJSON_AddStringToObject(result, "type", type);
            cJSON_AddNumberToObject(result, "value", value);
            cJSON_AddStringToObject(result, "unit", "raw"); /* TODO: per-sensor unit */
            return make_result(id, result);
        }
    }

    return make_error(id, -32602, "unknown sensor type");
}

/* ── Module wiring ─────────────────────────────────────────────────────── */

static esp_err_t sensor_init(int pin)
{
    (void)pin;
    /* TODO: per-sensor init — e.g. I2C bus init, ADC calibration */
    return ESP_OK;
}

static const char *s_methods[] = {"sensor.read"};

/* Events this module can fire (registered but not yet triggered — needs PIR ISR) */
static const char *s_events[] = {"sensor.threshold_exceeded"};

static void sensor_register_methods(void)
{
    mcpiot_rpc_register("sensor.read", handle_sensor_read);
    ESP_LOGI(TAG, "registered sensor.read");
}

static void sensor_register_events(void)
{
    mcpiot_event_register("sensor.threshold_exceeded");
    /*
     * TODO: start a FreeRTOS task that polls sensor values and calls:
     *   mcpiot_event_fire("sensor.threshold_exceeded",
     *       "{\"type\":\"temperature\",\"value\":38}");
     * when a threshold is crossed.
     */
}

const mcpiot_module_driver_t sensor_module_driver = {
    .type = "sensor",
    .pin = -1,
    .init = sensor_init,
    .register_methods = sensor_register_methods,
    .register_events = sensor_register_events,
    .methods = s_methods,
    .method_count = 1,
    .events = s_events,
    .event_count = 1,
};
