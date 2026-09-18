#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

static const char *TAG = "ac_hal";

/* Two I2C buses since v1.1.
 *
 * g_i2c    sensor bus: SGP40 + SCD41 on the C6's LP_I2C (GPIO6/7, fixed pads),
 *          with our own 4.7k pull-ups on the switched sensor rail.
 * g_gauge  the Feather's own bus to its MAX17048 (GPIO19/18).  Its pull-ups
 *          sit on VSENSOR, the LDO that GPIO20 switches - the same LDO that
 *          feeds the Feather's WS2812B, which idles at around a milliamp.  So
 *          this bus only exists while a battery read is in progress.
 *
 * See EDR-11 in docs/ENGINEERING_DECISIONS.md for how this was found. */
i2c_master_bus_handle_t g_i2c;
i2c_master_bus_handle_t g_gauge;

static esp_err_t sensor_bus_open(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = LP_I2C_NUM_0,
        .sda_io_num = AC_PIN_SENS_SDA,
        .scl_io_num = AC_PIN_SENS_SCL,
        .lp_source_clk = LP_I2C_SCLK_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* 4.7k on the carrier, to +3V3_SENS.  No internal pull-ups: they would
         * stay connected to the always-on domain when the rail is off. */
        .flags.enable_internal_pullup = false,
    };
    return i2c_new_master_bus(&bus, &g_i2c);
}

static esp_err_t out_pin(int pin, int level)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    return gpio_set_level(pin, level);
}

esp_err_t ac_hal_init(void)
{
    /* Both load switches start open, and the Feather's VSENSOR LDO starts
     * off.  Keeping the order explicit means a reset never leaves the 5 V
     * boost enabled while the firmware decides what to do. */
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_SPS30, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_SENS, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_I2C_PWR, 0));
    ESP_ERROR_CHECK(ac_led_init());
    return ac_rail_sensors(true);
}

esp_err_t ac_rail_sps30(bool on)
{
    esp_err_t err = gpio_set_level(AC_PIN_EN_SPS30, on ? 1 : 0);
    if (err != ESP_OK) return err;
    if (on) {
        /* TPS22918 turn-on plus TPS61023 soft start, then the SPS30's own
         * boot.  The datasheet does not give a boot time, so we allow the
         * documented 20 ms power-up plus margin before talking to it. */
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

esp_err_t ac_rail_sensors(bool on)
{
    if (!on) {
        /* The pull-ups live on this rail, so switching it off also removes
         * every path into the unpowered sensors - no need to hold the lines.
         * Devices are detached first so the drivers do not keep handles to a
         * bus that no longer exists. */
        ac_sgp40_detach();
        ac_scd41_detach();
        if (g_i2c) {
            i2c_del_master_bus(g_i2c);
            g_i2c = NULL;
        }
        return gpio_set_level(AC_PIN_EN_SENS, 0);
    }

    esp_err_t err = gpio_set_level(AC_PIN_EN_SENS, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!g_i2c) {
        err = sensor_bus_open();
        if (err != ESP_OK) ESP_LOGE(TAG, "sensor bus: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ac_gauge_bus(bool on)
{
    if (on) {
        gpio_set_level(AC_PIN_I2C_PWR, 1);
        vTaskDelay(pdMS_TO_TICKS(3));           /* LDO start-up */
        if (g_gauge) return ESP_OK;
        i2c_master_bus_config_t bus = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AC_PIN_GAUGE_SDA,
            .scl_io_num = AC_PIN_GAUGE_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = false,   /* R3 on the Feather */
        };
        return i2c_new_master_bus(&bus, &g_gauge);
    }
    ac_battery_detach();
    if (g_gauge) {
        i2c_del_master_bus(g_gauge);
        g_gauge = NULL;
    }
    /* Release both lines: with VSENSOR off, R3 pulls them to ~0 V, and a line
     * driven high here would back-feed VSENSOR (and the WS2812B) through R3. */
    gpio_set_direction(AC_PIN_GAUGE_SDA, GPIO_MODE_INPUT);
    gpio_set_direction(AC_PIN_GAUGE_SCL, GPIO_MODE_INPUT);
    return gpio_set_level(AC_PIN_I2C_PWR, 0);
}

void ac_rail_hold(bool hold)
{
    const int pins[] = { AC_PIN_EN_SPS30, AC_PIN_EN_SENS };
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (hold) gpio_hold_en(pins[i]);
        else      gpio_hold_dis(pins[i]);
    }
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
    /* On parts that can only hold every pad at once, the per-pin hold above
     * is not enough and the global deep-sleep hold has to be armed as well.
     * The ESP32-C6 holds individual pads, so gpio_hold_en() already did it and
     * this API does not exist there. */
    if (hold) gpio_deep_sleep_hold_en();
    else      gpio_deep_sleep_hold_dis();
#endif
}
