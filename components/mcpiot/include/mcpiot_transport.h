#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MCPIOT_TRANSPORT_UART = 0,
    // mcpiot_TRANSPORT_WIFI_TCP,   // future
    // mcpiot_TRANSPORT_BLE,        // future
} mcpiot_transport_type_t;

typedef struct {
    mcpiot_transport_type_t type;
    int uart_num;     // 0 = UART_NUM_0, 1 = UART_NUM_1, etc.
    int baud_rate;
    int tx_pin;       // -1 = use chip default
    int rx_pin;       // -1 = use chip default
} mcpiot_transport_config_t;

void mcpiot_transport_init(const mcpiot_transport_config_t *config);
void mcpiot_transport_start(void);
void mcpiot_transport_send(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
