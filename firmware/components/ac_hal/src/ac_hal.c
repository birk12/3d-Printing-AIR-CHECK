#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

#if SOC_RTCIO_PIN_COUNT > 0
#include "driver/rtc_io.h"
#endif

static const char *TAG = "ac_hal";

i2c_master_bus_handle_t g_i2c;   /* shared by the sensor drivers */

static esp_err_t i2c_bus_open(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AC_PIN_I2C_SDA,
        .scl_io_num = AC_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* The Feather already has 5k1 pull-ups to the always-on 3V3 rail.
         * Enabling the internal ones as well would slow the edges. */
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
    /* All three load switches start open.  The sensor rail is closed again a
     * few lines down; keeping the order explicit means a reset never leaves
     * the 5 V boost enabled while the firmware decides what to do. */
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_SPS30, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_SENS, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_EPD, 0));
    /* The Feather's second LDO feeds the NeoPixel and the STEMMA QT port.
     * Neither is used, and leaving it on costs about 60 uA. */
    ESP_ERROR_CHECK(out_pin(AC_PIN_STEMMA_PWR, 0));

    esp_err_t err = i2c_bus_open();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(ac_rail_sensors(true));
    return ESP_OK;
}

esp_err_t ac_rail_sps30(bool on)
{
    esp_err_t err = gpio_set_level(AC_PIN_EN_SPS30, on ? 1 : 0);
    if (err != ESP_OK) return err;
    if (on) {
        /* TPS22918 turn-on plus TPS61023 soft start, then the SPS30's own
         * boot.  The datasheet does not give a boot time, so we use the
         * documented 20 ms power-up plus margin before talking to it. */
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

esp_err_t ac_rail_sensors(bool on)
{
    if (!on) {
        /* With the rail down, the board's 5k1 pull-ups would push about
         * 0.5 mA per line into the unpowered sensors' ESD clamps.  Holding
         * both lines low removes the path entirely.  The bus has to be torn
         * down first, otherwise the I2C peripheral still owns the pads and
         * the GPIO writes do nothing.
         *
         * This is also why the rail is only ever switched off while the CPU is
         * awake: GPIO18/19 are not RTC pins and cannot be held through deep
         * sleep. In normal operation the rail stays up. */
        /* Every device has to be detached before the bus can be deleted, and
         * the drivers have to forget their handles or they would use a
         * dangling one after the rail comes back. */
        ac_sgp40_detach();
        ac_scd41_detach();
        ac_battery_detach();
        if (g_i2c) {
            i2c_del_master_bus(g_i2c);
            g_i2c = NULL;
        }
        gpio_set_direction(AC_PIN_I2C_SDA, GPIO_MODE_OUTPUT);
        gpio_set_direction(AC_PIN_I2C_SCL, GPIO_MODE_OUTPUT);
        gpio_set_level(AC_PIN_I2C_SDA, 0);
        gpio_set_level(AC_PIN_I2C_SCL, 0);
        return gpio_set_level(AC_PIN_EN_SENS, 0);
    }

    esp_err_t err = gpio_set_level(AC_PIN_EN_SENS, 1);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!g_i2c) {
        err = i2c_bus_open();
        if (err != ESP_OK) ESP_LOGE(TAG, "i2c reopen: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ac_rail_epd(bool on)
{
    esp_err_t err = gpio_set_level(AC_PIN_EN_EPD, on ? 1 : 0);
    if (on) vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}

void ac_rail_hold(bool hold)
{
#if SOC_RTCIO_PIN_COUNT > 0
    const int pins[] = { AC_PIN_EN_SPS30, AC_PIN_EN_SENS, AC_PIN_EN_EPD };
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
#else
    (void)hold;
#endif
}
