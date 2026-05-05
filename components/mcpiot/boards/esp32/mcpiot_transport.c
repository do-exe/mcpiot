#include "mcpiot_transport.h"
#include "mcpiot_rpc.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define MCPIOT_LINE_BUF      512
#define MCPIOT_RX_TASK_STACK 8192   /* WiFi IDF calls need ≥6 KB stack */
#define MCPIOT_USB_RX_BUF    512
#define MCPIOT_USB_TX_BUF    512

static SemaphoreHandle_t s_tx_mutex = NULL;

/* ── Send ───────────────────────────────────────────────────────────── */

void mcpiot_transport_send(const char *data, size_t len)
{
    if (!data || len == 0) return;
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(100));
        usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(100));
        xSemaphoreGive(s_tx_mutex);
    }
}

/* ── RX task — reads lines, dispatches to RPC ───────────────────────── */

static void usb_rx_task(void *arg)
{
    char *line = malloc(MCPIOT_LINE_BUF);
    if (!line) {
        vTaskDelete(NULL);
        return;
    }
    int pos = 0;

    while (1) {
        uint8_t byte;
        int n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100));
        if (n <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (pos == 0) continue;
            line[pos] = '\0';
            pos = 0;

            char *response = mcpiot_rpc_process(line);
            if (response) {
                mcpiot_transport_send(response, strlen(response));
                free(response);
            }
        } else {
            if (pos < MCPIOT_LINE_BUF - 1) {
                line[pos++] = (char)byte;
            } else {
                pos = 0;  /* line too long — discard and reset */
            }
        }
    }
    free(line);
    vTaskDelete(NULL);
}

/* ── Init / Start ───────────────────────────────────────────────────── */

void mcpiot_transport_init(const mcpiot_transport_config_t *config)
{
    s_tx_mutex = xSemaphoreCreateMutex();

    usb_serial_jtag_driver_config_t usb_cfg = {
        .rx_buffer_size = MCPIOT_USB_RX_BUF,
        .tx_buffer_size = MCPIOT_USB_TX_BUF,
    };
    usb_serial_jtag_driver_install(&usb_cfg);
}

void mcpiot_transport_start(void)
{
    xTaskCreate(usb_rx_task, "mcpiot_rx", MCPIOT_RX_TASK_STACK,
                NULL, 5, NULL);
}
