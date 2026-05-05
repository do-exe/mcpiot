/*
 * boards/stm32/mcpiot_transport.c — STM32 transport implementation (STUB)
 *
 * HOW TO USE THIS FILE:
 * ---------------------
 * When targeting STM32, in CMakeLists.txt swap:
 *   "boards/esp32/mcpiot_transport.c"  →  "boards/stm32/mcpiot_transport.c"
 * Nothing else changes.
 *
 * TRANSPORT OPTIONS for STM32 (pick one and implement below):
 *   - UART:     HAL_UART_Transmit() / HAL_UART_Receive_IT()
 *   - USB CDC:  CDC_Transmit_FS()   / CDC_Receive_FS() (CubeMX USB Device)
 *   - SWO/ITM:  ITM_SendChar()       (debug only, not bidirectional)
 *
 * REQUIRED includes (uncomment when implementing):
 *   #include "usbd_cdc_if.h"    // for USB CDC
 *   #include "usart.h"          // for UART (CubeMX generated)
 *   #include "cmsis_os.h"       // for FreeRTOS on STM32 (if used)
 */

#include "mcpiot_transport.h"
#include "mcpiot_rpc.h"
#include <string.h>
#include <stdlib.h>

/* ── Send ──────────────────────────────────────────────────────────────────── */

void mcpiot_transport_send(const char *data, size_t len)
{
    /*
     * TODO STM32 UART:
     *   HAL_UART_Transmit(&huart2, (uint8_t *)data, len, HAL_MAX_DELAY);
     *   HAL_UART_Transmit(&huart2, (uint8_t *)"\n", 1, HAL_MAX_DELAY);
     *
     * TODO STM32 USB CDC:
     *   CDC_Transmit_FS((uint8_t *)data, len);
     */
    (void)data;
    (void)len;
}

/* ── Init ──────────────────────────────────────────────────────────────────── */

void mcpiot_transport_init(const mcpiot_transport_config_t *config)
{
    /*
     * TODO STM32 UART:
     *   UART is typically already initialised by CubeMX-generated MX_USARTx_UART_Init().
     *   Just store the config if needed.
     *
     * TODO STM32 USB CDC:
     *   MX_USB_DEVICE_Init() — called from main before mcpiot_init().
     *   Register your CDC receive callback here.
     */
    (void)config;
}

/* ── Start (RX listener) ───────────────────────────────────────────────────── */

void mcpiot_transport_start(void)
{
    /*
     * TODO STM32:
     *   Option A — UART interrupt:
     *     HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1);
     *     // In HAL_UART_RxCpltCallback: accumulate bytes, call mcpiot_rpc_process()
     *
     *   Option B — FreeRTOS task (same pattern as ESP32):
     *     xTaskCreate(stm32_rx_task, "mcpiot_rx", 512, NULL, 5, NULL);
     *
     *   Option C — USB CDC receive callback:
     *     Override CDC_Receive_FS() in usbd_cdc_if.c to call mcpiot_rpc_process()
     */
}
