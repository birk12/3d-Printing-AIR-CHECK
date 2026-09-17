/* One multifunction button on GPIO6, active low.
 *
 *   short press   (< 1 s)        wake the display / next screen
 *   long press    (3 .. 8 s)     open commissioning
 *   very long     (12 .. 20 s)   factory reset
 *   over 20 s                    ignored, so a jammed button cannot wipe the
 *                                device
 *
 * GPIO6 is one of the ESP32-C6's RTC pins, which is what lets it wake the chip
 * from deep sleep through EXT1.
 */
#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";
static int64_t s_down_us;
static bool s_was_down;

esp_err_t ac_button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << AC_PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   /* the board has 100k as well */
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

bool ac_button_pressed(void) { return gpio_get_level(AC_PIN_BUTTON) == 0; }

ac_button_event_t ac_button_poll(void)
{
    bool down = ac_button_pressed();
    int64_t now = esp_timer_get_time();

    if (down && !s_was_down) {
        s_down_us = now;
        s_was_down = true;
        return AC_BTN_NONE;
    }
    if (!down && s_was_down) {
        s_was_down = false;
        uint32_t held = (uint32_t)((now - s_down_us) / 1000);
        if (held < 40) return AC_BTN_NONE;              /* contact bounce */
        if (held > AC_BTN_ABORT_MS) {
            ESP_LOGW(TAG, "held %lu ms, ignoring", (unsigned long)held);
            return AC_BTN_NONE;
        }
        if (held >= AC_BTN_VERY_LONG_MS) return AC_BTN_VERY_LONG;
        if (held >= AC_BTN_LONG_MS) return AC_BTN_LONG;
        return AC_BTN_SHORT;
    }
    return AC_BTN_NONE;
}
