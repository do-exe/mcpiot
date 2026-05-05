/*
 * boards/esp32/mcpiot_transport_ble.c — ESP32 BLE transport (STUB)
 *
 * WHEN TO USE THIS FILE:
 * ----------------------
 * Replace boards/esp32/mcpiot_transport.c (USB-JTAG) with this file in
 * CMakeLists.txt when you want the device to communicate over Bluetooth LE.
 * Useful for battery-powered IoT devices without WiFi.
 *
 * HOW IT WILL WORK:
 * -----------------
 * Device advertises as a BLE peripheral with a custom GATT service.
 * AI/client connects, uses Write characteristic to send JSON-RPC requests,
 * Notify characteristic to receive JSON-RPC responses.
 *
 * GATT service layout:
 *   Service UUID:    MCPIOT_BLE_SERVICE_UUID
 *   Characteristic:  RX (Write) — client writes JSON-RPC request
 *   Characteristic:  TX (Notify) — device notifies JSON-RPC response
 *
 * REQUIRED CMakeLists.txt REQUIRES additions:
 *   bt  esp_bt (NimBLE stack recommended — lighter than Bluedroid)
 *
 * IMPLEMENTATION STEPS (using NimBLE):
 * -------------------------------------
 *   Step 1 — NimBLE init:
 *     esp_nimble_hci_init();
 *     nimble_port_init();
 *     ble_svc_gap_init();
 *     ble_svc_gatt_init();
 *
 *   Step 2 — Register GATT service with RX/TX characteristics:
 *     static const struct ble_gatt_svc_def s_gatt_svcs[] = { ... };
 *     ble_gatts_count_cfg(s_gatt_svcs);
 *     ble_gatts_add_svcs(s_gatt_svcs);
 *
 *   Step 3 — RX callback (called when client writes):
 *     static int on_write(struct ble_gatt_access_ctxt *ctxt, ...) {
 *         // extract JSON from ctxt->om
 *         char *response = mcpiot_rpc_process(json_buf);
 *         // store response, trigger notify
 *     }
 *
 *   Step 4 — Send (notify):
 *     struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
 *     ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
 *
 *   Step 5 — Start advertising:
 *     nimble_port_freertos_init(ble_host_task);
 */

#include "mcpiot_transport.h"
#include "mcpiot_rpc.h"
#include <string.h>
#include <stdlib.h>

void mcpiot_transport_send(const char *data, size_t len)
{
    /* TODO: ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om); */
    (void)data;
    (void)len;
}

void mcpiot_transport_init(const mcpiot_transport_config_t *config)
{
    /* TODO: NimBLE stack init, GATT service registration */
    (void)config;
}

void mcpiot_transport_start(void)
{
    /* TODO: nimble_port_freertos_init(ble_host_task); + start advertising */
}
