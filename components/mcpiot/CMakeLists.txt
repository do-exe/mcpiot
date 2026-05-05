# ┌─────────────────────────────────────────────────────────────────────┐
# │  BOARD SELECTION — swap these two lines when changing target board  │
# │                                                                     │
# │  ESP32-S3 (active):                                                 │
# │    "boards/esp32/mcpiot_hal.c"                                      │
# │    "boards/esp32/mcpiot_transport.c"                                │
# │                                                                     │
# │  STM32 (future):                                                    │
# │    "boards/stm32/mcpiot_hal.c"                                      │
# │    "boards/stm32/mcpiot_transport.c"                                │
# └─────────────────────────────────────────────────────────────────────┘

idf_component_register(
    SRCS
        # core — board-agnostic, never changes
        "core/mcpiot.c"
        "core/mcpiot_registry.c"
        "core/mcpiot_rpc.c"
        "core/mcpiot_event.c"
        "core/cJSON.c"

        # board — swap folder name to change target board
        "boards/esp32/mcpiot_hal.c"
        "boards/esp32/mcpiot_transport.c"

    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_usb_serial_jtag
        log
        freertos
)
