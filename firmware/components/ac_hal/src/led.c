/* Status LED: Adafruit 159, 5 mm diffused RGB, common anode on the 4.0 V
 * regulator output.
 *
 * On  = drive the cathode low.
 * Off = release the pin (high impedance), never drive it high: with the anode
 *       at 4.0 V a driven-high cathode would still leave 0.9 V across
 *       LED and resistor.  That is below every forward voltage, so it would be
 *       dark, but high impedance is also dark and cannot surprise anyone. */
#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"

static const int k_pins[3] = { AC_PIN_LED_R, AC_PIN_LED_G, AC_PIN_LED_B };

esp_err_t ac_led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << AC_PIN_LED_R) | (1ULL << AC_PIN_LED_G) |
                        (1ULL << AC_PIN_LED_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

void ac_led_set(uint8_t rgb)
{
    for (int i = 0; i < 3; i++) {
        if (rgb & (1u << i)) {
            gpio_set_level(k_pins[i], 0);
            gpio_set_direction(k_pins[i], GPIO_MODE_OUTPUT);
        } else {
            gpio_set_direction(k_pins[i], GPIO_MODE_INPUT);
        }
    }
}
