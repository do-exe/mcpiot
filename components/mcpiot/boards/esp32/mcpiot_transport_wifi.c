/*
 * boards/esp32/mcpiot_transport_wifi.c — ESP32 WiFi TCP transport (STUB)
 *
 * WHEN TO USE THIS FILE:
 * ----------------------
 * Replace boards/esp32/mcpiot_transport.c (USB-JTAG) with this file in
 * CMakeLists.txt when you want the device to be accessible over WiFi.
 * Everything above the transport layer (RPC, registry, modules) stays unchanged.
 *
 * HOW IT WILL WORK:
 * -----------------
 * Device connects to WiFi AP → opens a TCP server on a fixed port (e.g. 3333)
 * AI client connects to: tcp://device_ip:3333
 * Sends JSON-RPC lines, reads JSON-RPC responses — same protocol as USB.
 *
 * For streaming (camera, audio):
 *   AI calls stream.open → device returns a second port (e.g. 3334)
 *   AI opens a second TCP connection to that port for raw binary frames
 *
 * REQUIRED CMakeLists.txt REQUIRES additions:
 *   esp_wifi  esp_netif  lwip  nvs_flash
 *
 * IMPLEMENTATION STEPS:
 * ---------------------
 *   Step 1 — WiFi station init:
 *     esp_netif_init();
 *     esp_event_loop_create_default();
 *     esp_netif_create_default_wifi_sta();
 *     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
 *     esp_wifi_init(&cfg);
 *     esp_wifi_set_mode(WIFI_MODE_STA);
 *     esp_wifi_set_config(WIFI_IF_STA, &wifi_config);  // SSID + password
 *     esp_wifi_start();
 *     esp_wifi_connect();
 *
 *   Step 2 — TCP server task:
 *     int sock = socket(AF_INET, SOCK_STREAM, 0);
 *     bind(sock, &addr, sizeof(addr));
 *     listen(sock, 1);
 *     int client = accept(sock, NULL, NULL);
 *     // read lines from client, call mcpiot_rpc_process(), send response
 *
 *   Step 3 — send:
 *     send(s_client_sock, data, len, 0);
 */

#include "mcpiot_transport.h"
#include "mcpiot_rpc.h"
#include <string.h>
#include <stdlib.h>

void mcpiot_transport_send(const char *data, size_t len)
{
    /* TODO: send(s_client_sock, data, len, 0); */
    (void)data;
    (void)len;
}

void mcpiot_transport_init(const mcpiot_transport_config_t *config)
{
    /* TODO: WiFi init, TCP server socket setup */
    (void)config;
}

void mcpiot_transport_start(void)
{
    /* TODO: xTaskCreate(wifi_tcp_server_task, "mcpiot_rx", 4096, NULL, 5, NULL); */
}
