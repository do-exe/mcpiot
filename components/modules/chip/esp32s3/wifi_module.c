#include "wifi_module.h"
#include "mcpiot_rpc.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "wifi_module";

/* ── Internal state ────────────────────────────────────────────────────── */

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_CONNECT_TIMEOUT_MS 15000

static EventGroupHandle_t s_wifi_eg = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_wifi_started = false;
static char s_connected_ssid[33] = {0};

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

/* ── WiFi event handler ────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "STA disconnected");
        if (s_wifi_eg)
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        s_connected_ssid[0] = '\0';
    }
    else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        if (s_wifi_eg)
            xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

/* ── One-time WiFi stack init ──────────────────────────────────────────── */

static esp_err_t wifi_stack_init(void)
{
    if (s_wifi_started)
        return ESP_OK;

    /* NVS required by WiFi driver */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
        return ret;

    esp_netif_init();
    esp_event_loop_create_default();

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_eg = xEventGroupCreate();
    s_wifi_started = true;
    ESP_LOGI(TAG, "WiFi stack started");
    return ESP_OK;
}

/* ── RPC: wifi.scan ────────────────────────────────────────────────────── */

/*
 * wifi.scan  {}
 * Returns: [{"ssid":"...", "rssi":-60, "auth":"WPA2"}, ...]
 */
static char *handle_wifi_scan(cJSON *params, int id)
{
    (void)params;

    if (wifi_stack_init() != ESP_OK)
        return make_error(id, -32000, "WiFi init failed");

    /* Blocking scan — up to 20 APs */
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };
    esp_wifi_scan_start(&scan_cfg, true /* blocking */);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0)
    {
        cJSON *arr = cJSON_CreateArray();
        return make_result(id, arr);
    }

    uint16_t max = ap_count < 20 ? ap_count : 20;
    wifi_ap_record_t *records = calloc(max, sizeof(wifi_ap_record_t));
    if (!records)
        return make_error(id, -32000, "out of memory");

    esp_wifi_scan_get_ap_records(&max, records);

    cJSON *arr = cJSON_CreateArray();
    static const char *auth_names[] = {
        "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK",
        "WPA2_ENTERPRISE", "WPA3_PSK", "WPA2_WPA3_PSK", "WAPI_PSK", "OWE"};
    for (int i = 0; i < max; i++)
    {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        int auth = (int)records[i].authmode;
        const char *auth_str = (auth >= 0 && auth < 10) ? auth_names[auth] : "UNKNOWN";
        cJSON_AddStringToObject(ap, "auth", auth_str);
        cJSON_AddItemToArray(arr, ap);
    }
    free(records);
    return make_result(id, arr);
}

/* ── RPC: wifi.connect ─────────────────────────────────────────────────── */

/*
 * wifi.connect  {"ssid": "MyNet", "password": "secret"}
 * Returns: {"ok": true, "ip": "192.168.1.42"} or error
 */
static char *handle_wifi_connect(cJSON *params, int id)
{
    cJSON *ssid_item = params ? cJSON_GetObjectItem(params, "ssid") : NULL;
    cJSON *pass_item = params ? cJSON_GetObjectItem(params, "password") : NULL;

    if (!ssid_item || !cJSON_IsString(ssid_item))
        return make_error(id, -32602, "ssid required");

    const char *ssid = ssid_item->valuestring;
    /* password is optional — omit or empty string means open network */
    const char *pass = (pass_item && cJSON_IsString(pass_item))
                       ? pass_item->valuestring : "";

    if (strlen(ssid) > 32)
        return make_error(id, -32602, "ssid too long (max 32)");
    if (strlen(pass) > 64)
        return make_error(id, -32602, "password too long (max 64)");

    if (wifi_stack_init() != ESP_OK)
        return make_error(id, -32000, "WiFi init failed");

    /* Disconnect any previous session */
    esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi_eg, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    bool open_network = (strlen(pass) == 0);
    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = open_network ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());
    ESP_LOGI(TAG, "Connecting to %s …", ssid);

    /* Wait for result */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT)
    {
        strlcpy(s_connected_ssid, ssid, sizeof(s_connected_ssid));

        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(s_sta_netif, &ip_info);
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));

        cJSON *result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "ok", true);
        cJSON_AddStringToObject(result, "ssid", ssid);
        cJSON_AddStringToObject(result, "ip", ip_str);
        return make_result(id, result);
    }

    if (bits & WIFI_FAIL_BIT)
        return make_error(id, -32000, "Connection failed — wrong password or AP unreachable");

    return make_error(id, -32000, "Connection timeout");
}

/* ── RPC: wifi.status ──────────────────────────────────────────────────── */

/*
 * wifi.status  {}
 * Returns: {"connected": true/false, "ssid": "...", "ip": "...", "rssi": -60}
 */
static char *handle_wifi_status(cJSON *params, int id)
{
    (void)params;

    cJSON *result = cJSON_CreateObject();

    if (!s_wifi_started)
    {
        cJSON_AddBoolToObject(result, "connected", false);
        cJSON_AddStringToObject(result, "ssid", "");
        cJSON_AddStringToObject(result, "ip", "");
        cJSON_AddNumberToObject(result, "rssi", 0);
        return make_result(id, result);
    }

    wifi_ap_record_t ap_info;
    bool connected = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);

    cJSON_AddBoolToObject(result, "connected", connected);

    if (connected)
    {
        cJSON_AddStringToObject(result, "ssid", (char *)ap_info.ssid);
        cJSON_AddNumberToObject(result, "rssi", ap_info.rssi);

        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(s_sta_netif, &ip_info);
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(result, "ip", ip_str);
    }
    else
    {
        cJSON_AddStringToObject(result, "ssid", "");
        cJSON_AddStringToObject(result, "ip", "");
        cJSON_AddNumberToObject(result, "rssi", 0);
    }

    return make_result(id, result);
}

/* ── RPC: wifi.disconnect ──────────────────────────────────────────────── */

/*
 * wifi.disconnect  {}
 * Returns: {"ok": true}
 */
static char *handle_wifi_disconnect(cJSON *params, int id)
{
    (void)params;
    esp_wifi_disconnect();
    s_connected_ssid[0] = '\0';
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    return make_result(id, result);
}

/* ── Module wiring ─────────────────────────────────────────────────────── */

static esp_err_t wifi_init(int pin)
{
    (void)pin;
    /* Eagerly start the WiFi stack at module load time so the radio is
     * warm before the first RPC call arrives — avoids a >10 s cold-start
     * delay on the first wifi.scan / wifi.connect call.              */
    esp_err_t ret = wifi_stack_init();
    if (ret != ESP_OK)
        ESP_LOGW(TAG, "WiFi stack pre-init failed: %d (will retry on RPC)", ret);
    return ESP_OK;
}

static const char *s_methods[] = {
    "wifi.scan", "wifi.connect", "wifi.status", "wifi.disconnect"};

static void wifi_register_methods(void)
{
    mcpiot_rpc_register("wifi.scan", handle_wifi_scan);
    mcpiot_rpc_register("wifi.connect", handle_wifi_connect);
    mcpiot_rpc_register("wifi.status", handle_wifi_status);
    mcpiot_rpc_register("wifi.disconnect", handle_wifi_disconnect);
    ESP_LOGI(TAG, "registered wifi.scan + wifi.connect + wifi.status + wifi.disconnect");
}

static void wifi_register_events(void) { /* no push events yet */ }

const mcpiot_module_driver_t wifi_module_driver = {
    .type = "wifi",
    .pin = -1,
    .init = wifi_init,
    .register_methods = wifi_register_methods,
    .register_events = wifi_register_events,
    .methods = s_methods,
    .method_count = 4,
    .events = NULL,
    .event_count = 0,
};
