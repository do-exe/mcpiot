#include "mcpiot_hal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <string.h>

/* ── PWM channel pool — maps pin → LEDC channel + timer ───────────────────
 *
 * ESP32-S3 low-speed LEDC: 8 channels, 4 timers (TIMER_0..TIMER_3).
 * Each slot gets its OWN timer (slot % 4), so changing one pin's frequency
 * never affects any other pin.  Supports up to 8 simultaneous PWM pins;
 * pins sharing a slot index (e.g. slot 0 and slot 4) share a timer and
 * therefore must use the same frequency — the second call wins.
 * In practice you'll rarely need more than 4 independent PWM frequencies.
 */
#define MAX_PWM_CHANNELS 8

typedef struct
{
    int pin;
    ledc_channel_t channel;
    ledc_timer_t timer;
    int freq_hz;
    bool in_use;
    bool configured; /* false = needs ledc_channel_config */
} pwm_slot_t;

static pwm_slot_t s_pwm[MAX_PWM_CHANNELS];
static bool s_pwm_init = false;

/* Returns slot index, or -1 if full. Sets *is_new. */
static int alloc_pwm_slot(int pin, bool *is_new)
{
    if (!s_pwm_init)
    {
        memset(s_pwm, 0, sizeof(s_pwm));
        for (int i = 0; i < MAX_PWM_CHANNELS; i++)
        {
            s_pwm[i].pin = -1;
            s_pwm[i].timer = (ledc_timer_t)(i % 4); /* each slot → own timer */
        }
        s_pwm_init = true;
    }
    /* reuse existing slot for same pin */
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (s_pwm[i].in_use && s_pwm[i].pin == pin)
        {
            *is_new = false;
            return i;
        }
    }
    /* allocate a free slot */
    for (int i = 0; i < MAX_PWM_CHANNELS; i++)
    {
        if (!s_pwm[i].in_use)
        {
            s_pwm[i].pin = pin;
            s_pwm[i].channel = (ledc_channel_t)i;
            s_pwm[i].in_use = true;
            s_pwm[i].configured = false;
            *is_new = true;
            return i;
        }
    }
    return -1; /* all 8 channels busy */
}

esp_err_t mcpiot_hal_gpio_set_input(int pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t mcpiot_hal_gpio_set_output(int pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

int mcpiot_hal_gpio_read(int pin)
{
    return gpio_get_level(pin);
}

esp_err_t mcpiot_hal_gpio_write(int pin, int level)
{
    return gpio_set_level(pin, level);
}

int mcpiot_hal_adc_read(int channel)
{
    (void)channel;
    return -1; /* TODO: implement via esp_adc */
}

esp_err_t mcpiot_hal_pwm_set(int pin, float duty_percent, int freq_hz)
{
    bool is_new = false;
    int idx = alloc_pwm_slot(pin, &is_new);
    if (idx < 0)
        return ESP_ERR_NO_MEM;

    ledc_channel_t ch = s_pwm[idx].channel;
    ledc_timer_t tmr = s_pwm[idx].timer;

    /* duty: 0-100 % → 0..8191  (13-bit, max valid = 2^13-1 = 8191)
     *
     * IMPORTANT: do NOT use 2^13 = 8192 as the 100% value.
     * At 13-bit resolution the LEDC counter runs 0..8191.  When duty = 8192,
     * the LOW transition at (hpoint + duty) mod 8192 = 0 lands on the same
     * cycle as the HIGH transition at hpoint = 0, so the output is always
     * LOW — indistinguishable from 0%.  The driver returns ESP_OK, making
     * the bug invisible.  Clamping to 8191 is the correct maximum.
     */
    uint32_t duty = (uint32_t)((duty_percent / 100.0f) * (float)((1u << 13) - 1));
    if (duty > (1u << 13) - 1)
        duty = (1u << 13) - 1;

    /* ── timer: (re-)configure on first use or frequency change ─────────
     *
     * WHY ledc_timer_pause is required before reconfiguring:
     *   LEDC_AUTO_CLK chooses the clock source based on the requested
     *   frequency.  Very low frequencies (< ~10 Hz at 13-bit) require the
     *   RTC slow clock; higher frequencies use APB (80 MHz).  Switching
     *   clock sources on a running timer is not allowed and ledc_timer_config
     *   returns an error if attempted without pausing first.
     */
    if (!s_pwm[idx].configured || s_pwm[idx].freq_hz != freq_hz)
    {
        if (s_pwm[idx].configured)
        {
            /* Pause the running timer so the driver allows clock-source changes */
            ledc_timer_pause(LEDC_LOW_SPEED_MODE, tmr);
        }

        ledc_timer_config_t timer_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .timer_num = tmr,
            .duty_resolution = LEDC_TIMER_13_BIT,
            .freq_hz = (uint32_t)freq_hz,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        esp_err_t ret = ledc_timer_config(&timer_cfg);
        if (ret != ESP_OK)
        {
            ESP_LOGE("mcpiot_hal", "ledc_timer_config pin=%d freq=%dHz err=0x%x", pin, freq_hz, ret);
            return ret;
        }
        s_pwm[idx].freq_hz = freq_hz;
        /* Force full channel re-init below: after a timer pause+reconfig the
         * channel must be re-bound to the new clock domain via ledc_channel_config. */
        s_pwm[idx].configured = false;
    }

    /* ── channel init / re-init ──────────────────────────────────────────
     *
     * ledc_channel_config is called with duty=0.  We intentionally do NOT
     * pass the real duty here.  Reason:
     *
     *   ESP32-S3 LEDC low-speed mode has a shared hardware "update" latch.
     *   When any channel calls ledc_update_duty (or ledc_channel_config
     *   triggers the internal update), the hardware flushes the SHADOW
     *   REGISTER of every low-speed channel to its output simultaneously.
     *
     *   If we pass duty=8192 to ledc_channel_config, that value goes to
     *   the OUTPUT register directly, but the SHADOW register is left at 0
     *   (never written).  The moment a SECOND channel configures itself and
     *   triggers the global update, channel-0's shadow(0) overwrites its
     *   output → pin that was at 100% suddenly goes off.
     *
     *   Solution: always initialise with duty=0 so shadow and output match,
     *   then use ledc_set_duty + ledc_update_duty (which writes the shadow
     *   first, then triggers the per-channel latch) for the actual value.
     *   This path is used for ALL calls — first-time and subsequent — so the
     *   shadow register is always authoritative and a global flush is safe.
     */
    if (!s_pwm[idx].configured)
    {
        ledc_channel_config_t ch_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = ch,
            .timer_sel = tmr,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = pin,
            .duty = 0, /* intentionally 0 — real duty set below */
            .hpoint = 0,
        };
        esp_err_t ret = ledc_channel_config(&ch_cfg);
        if (ret != ESP_OK)
        {
            ESP_LOGE("mcpiot_hal", "ledc_channel_config pin=%d err=0x%x", pin, ret);
            return ret;
        }
        s_pwm[idx].configured = true;
    }

    /* ── set duty (shadow → output) — used for ALL calls ─────────────────
     *
     * ledc_set_duty  : writes 'duty' into the shadow register
     * ledc_update_duty: triggers the per-channel latch to copy shadow→output
     *                   on the next PWM cycle boundary.
     * Calling these unconditionally means the shadow register is always
     * consistent regardless of what other channels are doing.
     */
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
    if (ret != ESP_OK)
    {
        ESP_LOGE("mcpiot_hal", "ledc_set_duty pin=%d err=0x%x", pin, ret);
        return ret;
    }
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
    if (ret != ESP_OK)
    {
        ESP_LOGE("mcpiot_hal", "ledc_update_duty pin=%d err=0x%x", pin, ret);
    }
    return ret;
}
