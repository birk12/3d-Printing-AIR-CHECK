#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

static const char *TAG = "ac_hal";

/* Two I2C buses, one per switched rail (v1.2).
 *
 * g_i2c    SGP40 breakout on the C6's LP_I2C (GPIO6/7, fixed pads), with the
 *          carrier's 4.7k pull-ups on +3V3_SENS.  Powered for ~0.25 s per VOC
 *          sample.
 * g_sen    SEN63C on the HP I2C controller (GPIO19/20), with 4.7k pull-ups on
 *          +3V3_SEN6X.  Powered for one measurement window an hour in ECO.
 *
 * A bus only exists while its rail is up.  When a rail drops, its pull-ups go
 * with it, and the two pins are released to inputs so that nothing drives
 * current into an unpowered sensor through its I/O protection diodes. */
i2c_master_bus_handle_t g_i2c;
i2c_master_bus_handle_t g_sen;

void ac_sen6x_detach(void);

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

static void release_pins(int a, int b)
{
    gpio_reset_pin(a);
    gpio_reset_pin(b);
    gpio_set_direction(a, GPIO_MODE_INPUT);
    gpio_set_direction(b, GPIO_MODE_INPUT);
    gpio_set_pull_mode(a, GPIO_FLOATING);
    gpio_set_pull_mode(b, GPIO_FLOATING);
}

esp_err_t ac_hal_init(void)
{
    /* Both load switches start open.  The FireBeetle's green LED sits on a
     * strapping pin through 2k to GND; driving it low keeps it dark. */
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_SEN6X, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_EN_SENS, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_BOARD_LED, 0));
    release_pins(AC_PIN_SENS_SDA, AC_PIN_SENS_SCL);
    release_pins(AC_PIN_SEN6X_SDA, AC_PIN_SEN6X_SCL);
    ESP_ERROR_CHECK(ac_led_init());
    return ac_battery_init();
}

esp_err_t ac_rail_sen6x(bool on)
{
    if (!on) {
        ac_sen6x_detach();
        if (g_sen) {
            i2c_del_master_bus(g_sen);
            g_sen = NULL;
        }
        release_pins(AC_PIN_SEN6X_SDA, AC_PIN_SEN6X_SCL);
        return gpio_set_level(AC_PIN_EN_SEN6X, 0);
    }

    esp_err_t err = gpio_set_level(AC_PIN_EN_SEN6X, 1);
    if (err != ESP_OK) return err;
    /* SEN6x datasheet Table 1: 100 ms from power-on until I2C.  The load
     * switch's own rise time is well under a millisecond without a CT cap. */
    vTaskDelay(pdMS_TO_TICKS(120));
    if (g_sen) return ESP_OK;
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AC_PIN_SEN6X_SDA,
        .scl_io_num = AC_PIN_SEN6X_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   /* R11/R12 on the carrier */
    };
    err = i2c_new_master_bus(&bus, &g_sen);
    if (err != ESP_OK) ESP_LOGE(TAG, "SEN6x bus: %s", esp_err_to_name(err));
    return err;
}

esp_err_t ac_rail_sensors(bool on)
{
    if (!on) {
        /* Devices are detached first so the drivers do not keep handles to a
         * bus that no longer exists. */
        ac_sgp40_detach();
        if (g_i2c) {
            i2c_del_master_bus(g_i2c);
            g_i2c = NULL;
        }
        release_pins(AC_PIN_SENS_SDA, AC_PIN_SENS_SCL);
        return gpio_set_level(AC_PIN_EN_SENS, 0);
    }

    esp_err_t err = gpio_set_level(AC_PIN_EN_SENS, 1);
    if (err != ESP_OK) return err;
    /* Load switch, then the breakout's AP2112 and the SGP40's 0.6 ms
     * power-up.  5 ms covers all three with margin. */
    vTaskDelay(pdMS_TO_TICKS(5));
    if (g_i2c) return ESP_OK;
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
    err = i2c_new_master_bus(&bus, &g_i2c);
    if (err != ESP_OK) ESP_LOGE(TAG, "sensor bus: %s", esp_err_to_name(err));
    return err;
}

void ac_rail_hold(bool hold)
{
    const int pins[] = { AC_PIN_EN_SEN6X, AC_PIN_EN_SENS };
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
