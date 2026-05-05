#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mcpiot.h"

void app_main(void)
{
    /*
     * Transport: UART0, 115200 baud, default pins
     * (GPIO43 TX / GPIO44 RX on ESP32-S3)
     * tx_pin / rx_pin = -1 means UART_PIN_NO_CHANGE (keep chip defaults)
     */
    mcpiot_config_t cfg = {
        .board   = "esp32s3",
        .version = "1.0.0",
        .transport = {
            .type      = MCPIOT_TRANSPORT_UART,
            .uart_num  = 0,
            .baud_rate = 115200,
            .tx_pin    = -1,
            .rx_pin    = -1,
        },
    };
    mcpiot_init(&cfg);

    /*
     * Register modules — in the future MCP-IoT Studio will generate this block
     * automatically based on the user's hardware selection.
     */
    const char *gpio_methods[] = {"gpio.read", "gpio.write"};
    mcpiot_registry_add_module("gpio", -1, gpio_methods, 2);

    mcpiot_start();

    /* MCP-IoT runs in its own task — keep app_main alive */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
