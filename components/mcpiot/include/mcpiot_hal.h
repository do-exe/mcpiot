#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── GPIO ──────────────────────────────────────────────────────────────── */
    esp_err_t mcpiot_hal_gpio_set_input(int pin);
    esp_err_t mcpiot_hal_gpio_set_output(int pin);
    int mcpiot_hal_gpio_read(int pin);
    esp_err_t mcpiot_hal_gpio_write(int pin, int level);

    /* ── ADC (stub — future) ─────────────────────────────────────────────── */
    int mcpiot_hal_adc_read(int channel);

    /* ── PWM / LEDC (stub — future) ─────────────────────────────────────── */
    esp_err_t mcpiot_hal_pwm_set(int pin, float duty_percent, int freq_hz);

#ifdef __cplusplus
}
#endif
