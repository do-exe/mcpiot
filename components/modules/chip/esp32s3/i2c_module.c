#include "i2c_module.h"
#include "mcpiot_rpc.h"
#include "cJSON.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "i2c_module";

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

/* ── Helpers ───────────────────────────────────────────────────────────── */

/*
 * Create a fresh I2C master bus on the given SDA/SCL pins.
 * Caller must call i2c_del_master_bus() when done.
 * Internal pull-ups are enabled — external pull-ups are better for reliability.
 */
static esp_err_t open_bus(int sda, int scl, i2c_master_bus_handle_t *out_bus)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port               = -1,       /* auto-select port */
        .sda_io_num             = (gpio_num_t)sda,
        .scl_io_num             = (gpio_num_t)scl,
        .clk_source             = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt      = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_cfg, out_bus);
}

static int get_int_field(cJSON *params, const char *key, int *out)
{
    cJSON *item = params ? cJSON_GetObjectItem(params, key) : NULL;
    if (!item || !cJSON_IsNumber(item)) return 0;
    *out = (int)item->valuedouble;
    return 1;
}

/* ── RPC: i2c.scan ─────────────────────────────────────────────────────── */

/*
 * i2c.scan  {"sda": N, "scl": N}
 *
 * Scans all 7-bit I2C addresses (0x08..0x77) and returns those that ACK.
 * An empty array means no devices found (or no pull-up resistors connected).
 *
 * Returns: [{"addr": 60, "hex": "0x3C"}, ...]
 */
static char *handle_i2c_scan(cJSON *params, int id)
{
    int sda, scl;
    if (!get_int_field(params, "sda", &sda) || !get_int_field(params, "scl", &scl))
        return make_error(id, -32602, "sda and scl pins required");

    i2c_master_bus_handle_t bus = NULL;
    if (open_bus(sda, scl, &bus) != ESP_OK)
        return make_error(id, -32000, "I2C bus init failed — check SDA/SCL pins");

    cJSON *arr = cJSON_CreateArray();
    char hex[8];

    for (int addr = 0x08; addr <= 0x77; addr++)
    {
        esp_err_t ret = i2c_master_probe(bus, (uint16_t)addr, 10 /* ms */);
        if (ret == ESP_OK)
        {
            snprintf(hex, sizeof(hex), "0x%02X", addr);
            cJSON *dev = cJSON_CreateObject();
            cJSON_AddNumberToObject(dev, "addr", addr);
            cJSON_AddStringToObject(dev, "hex",  hex);
            cJSON_AddItemToArray(arr, dev);
            ESP_LOGI(TAG, "i2c.scan found device at 0x%02X", addr);
        }
    }

    i2c_del_master_bus(bus);
    ESP_LOGI(TAG, "i2c.scan sda=%d scl=%d done", sda, scl);
    return make_result(id, arr);
}

/* ── RPC: i2c.read_reg ──────────────────────────────────────────────────── */

/*
 * i2c.read_reg  {"sda": N, "scl": N, "addr": N, "reg": N, "len": N}
 *
 * Writes one register byte then reads len bytes back (standard I2C register read).
 * addr = 7-bit device address (e.g. 0x3C for SSD1306).
 * reg  = register address to read from.
 * len  = number of bytes to read (1-32).
 *
 * Returns: {"addr": N, "reg": N, "data": [b0, b1, ...], "hex": "3C 00 FF ..."}
 */
static char *handle_i2c_read_reg(cJSON *params, int id)
{
    int sda, scl, addr, reg, len;
    if (!get_int_field(params, "sda",  &sda)  ||
        !get_int_field(params, "scl",  &scl)  ||
        !get_int_field(params, "addr", &addr) ||
        !get_int_field(params, "reg",  &reg)  ||
        !get_int_field(params, "len",  &len))
        return make_error(id, -32602, "sda, scl, addr, reg, len required");

    if (len < 1 || len > 32)
        return make_error(id, -32602, "len must be 1-32");

    i2c_master_bus_handle_t bus = NULL;
    if (open_bus(sda, scl, &bus) != ESP_OK)
        return make_error(id, -32000, "I2C bus init failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (uint16_t)addr,
        .scl_speed_hz    = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK)
    {
        i2c_del_master_bus(bus);
        return make_error(id, -32000, "I2C add device failed");
    }

    uint8_t reg_byte = (uint8_t)reg;
    uint8_t buf[32]  = {0};

    esp_err_t ret = i2c_master_transmit_receive(dev, &reg_byte, 1, buf, (size_t)len, 50);

    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);

    if (ret != ESP_OK)
        return make_error(id, -32000, "I2C read failed — NACK or no device");

    /* Build hex string "B0 B1 B2 ..." */
    char *hex_str = malloc((size_t)len * 3 + 1);
    if (!hex_str)
        return make_error(id, -32000, "out of memory");
    int pos = 0;
    for (int i = 0; i < len; i++)
        pos += snprintf(hex_str + pos, 4, i ? " %02X" : "%02X", buf[i]);

    cJSON *data_arr = cJSON_CreateArray();
    for (int i = 0; i < len; i++)
        cJSON_AddItemToArray(data_arr, cJSON_CreateNumber(buf[i]));

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "addr", addr);
    cJSON_AddNumberToObject(result, "reg",  reg);
    cJSON_AddItemToObject(result,   "data", data_arr);
    cJSON_AddStringToObject(result, "hex",  hex_str);
    free(hex_str);

    ESP_LOGI(TAG, "i2c.read_reg addr=0x%02X reg=0x%02X len=%d", addr, reg, len);
    return make_result(id, result);
}

/* ── RPC: i2c.write_reg ─────────────────────────────────────────────────── */

/*
 * i2c.write_reg  {"sda": N, "scl": N, "addr": N, "reg": N, "data": [N, ...]}
 *
 * Writes [reg, data[0], data[1], ...] to the device in a single transaction.
 * data = array of 1-31 bytes.
 *
 * Returns: {"ok": true, "addr": N, "reg": N, "bytes_written": N}
 */
static char *handle_i2c_write_reg(cJSON *params, int id)
{
    int sda, scl, addr, reg;
    if (!get_int_field(params, "sda",  &sda)  ||
        !get_int_field(params, "scl",  &scl)  ||
        !get_int_field(params, "addr", &addr) ||
        !get_int_field(params, "reg",  &reg))
        return make_error(id, -32602, "sda, scl, addr, reg required");

    cJSON *data_item = params ? cJSON_GetObjectItem(params, "data") : NULL;
    if (!data_item || !cJSON_IsArray(data_item))
        return make_error(id, -32602, "data array required");

    int data_len = cJSON_GetArraySize(data_item);
    if (data_len < 1 || data_len > 31)
        return make_error(id, -32602, "data array must be 1-31 bytes");

    /* Build write buffer: [reg, data[0], data[1], ...] */
    uint8_t buf[32];
    buf[0] = (uint8_t)reg;
    for (int i = 0; i < data_len; i++)
    {
        cJSON *byte_item = cJSON_GetArrayItem(data_item, i);
        if (!byte_item || !cJSON_IsNumber(byte_item))
            return make_error(id, -32602, "data array must contain numbers");
        buf[1 + i] = (uint8_t)(int)byte_item->valuedouble;
    }

    i2c_master_bus_handle_t bus = NULL;
    if (open_bus(sda, scl, &bus) != ESP_OK)
        return make_error(id, -32000, "I2C bus init failed");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = (uint16_t)addr,
        .scl_speed_hz    = 100000,
    };
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK)
    {
        i2c_del_master_bus(bus);
        return make_error(id, -32000, "I2C add device failed");
    }

    esp_err_t ret = i2c_master_transmit(dev, buf, (size_t)(1 + data_len), 50);

    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);

    if (ret != ESP_OK)
        return make_error(id, -32000, "I2C write failed — NACK or no device");

    ESP_LOGI(TAG, "i2c.write_reg addr=0x%02X reg=0x%02X %d bytes", addr, reg, data_len);

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result,   "ok",           true);
    cJSON_AddNumberToObject(result, "addr",         addr);
    cJSON_AddNumberToObject(result, "reg",          reg);
    cJSON_AddNumberToObject(result, "bytes_written", data_len);
    return make_result(id, result);
}

/* ── Module wiring ─────────────────────────────────────────────────────── */

static esp_err_t i2c_module_init(int pin)
{
    (void)pin;
    /* No persistent state — bus is created per call */
    ESP_LOGI(TAG, "I2C module ready (bus created per-call)");
    return ESP_OK;
}

static const char *s_methods[] = {"i2c.scan", "i2c.read_reg", "i2c.write_reg"};

static void i2c_register_methods(void)
{
    mcpiot_rpc_register("i2c.scan",      handle_i2c_scan);
    mcpiot_rpc_register("i2c.read_reg",  handle_i2c_read_reg);
    mcpiot_rpc_register("i2c.write_reg", handle_i2c_write_reg);
    ESP_LOGI(TAG, "registered i2c.scan + i2c.read_reg + i2c.write_reg");
}

static void i2c_register_events(void) {}

const mcpiot_module_driver_t i2c_module_driver = {
    .type             = "i2c",
    .pin              = -1,
    .init             = i2c_module_init,
    .register_methods = i2c_register_methods,
    .register_events  = i2c_register_events,
    .methods          = s_methods,
    .method_count     = 3,
    .events           = NULL,
    .event_count      = 0,
};
