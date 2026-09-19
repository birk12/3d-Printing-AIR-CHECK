#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

static const char *TAG = "ac_hal";

/* Buses (v1.3).
 *
 * g_i2c   LP_I2C on GPIO6/7, always up: SGP40 (SparkFun, 4.7k pull-ups on
 *         its jumper) and SHT40 (Seeed Grove).  Both boards run from 3.3 V
 *         permanently; neither has an LDO, and the SparkFun LED jumper is cut.
 * g_hp    the single HP I2C controller.  It is created on the SEN62's pins
 *         (GPIO19/20, pull-ups on the switched SEN62 supply) or on the
 *         Sunrise's pins (GPIO17/21, pull-ups on GPIO18), for as long as one
 *         of them is in use, and deleted afterwards.  When deleted, both pairs
 *         of pins are inputs, so nothing drives an unpowered sensor. */
i2c_master_bus_handle_t g_i2c;
i2c_master_bus_handle_t g_hp;
static int s_hp_sda = -1;

void ac_sen6x_detach(void);
void ac_sunrise_detach(void);

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

/* Route the HP controller to a pin pair.  Callers use it one at a time from
 * the measurement task, so there is no locking. */
esp_err_t ac_hp_bus_open(int sda, int scl)
{
    if (g_hp && s_hp_sda == sda) return ESP_OK;
    if (g_hp) return ESP_ERR_INVALID_STATE;          /* still held by the other */
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&bus, &g_hp);
    if (err == ESP_OK) s_hp_sda = sda;
    else ESP_LOGE(TAG, "HP I2C on %d/%d: %s", sda, scl, esp_err_to_name(err));
    return err;
}

void ac_hp_bus_close(int sda, int scl)
{
    if (g_hp && s_hp_sda == sda) {
        i2c_del_master_bus(g_hp);
        g_hp = NULL;
        s_hp_sda = -1;
    }
    release_pins(sda, scl);
}

static ac_wait_hook_t s_hook;

void ac_hal_set_wait_hook(ac_wait_hook_t hook) { s_hook = hook; }

void ac_hal_wait_ms(uint32_t ms)
{
    while (ms) {
        uint32_t slice = ms > 1000 ? 1000 : ms;
        vTaskDelay(pdMS_TO_TICKS(slice));
        ms -= slice;
        if (s_hook) s_hook();
    }
}

esp_err_t ac_hal_init(void)
{
    ESP_ERROR_CHECK(out_pin(AC_PIN_SEN_EN, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_CO2_EN, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_CO2_IO, 0));
    ESP_ERROR_CHECK(out_pin(AC_PIN_BOARD_LED, 0));
    release_pins(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL);
    release_pins(AC_PIN_CO2_SDA, AC_PIN_CO2_SCL);
    ESP_ERROR_CHECK(ac_led_init());

    i2c_master_bus_config_t lp = {
        .i2c_port = LP_I2C_NUM_0,
        .sda_io_num = AC_PIN_SENS_SDA,
        .scl_io_num = AC_PIN_SENS_SCL,
        .lp_source_clk = LP_I2C_SCLK_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,        /* on the boards */
    };
    esp_err_t err = i2c_new_master_bus(&lp, &g_i2c);
    if (err != ESP_OK) ESP_LOGE(TAG, "LP I2C: %s", esp_err_to_name(err));
    return ac_battery_init();
}

esp_err_t ac_rail_sen6x(bool on)
{
    if (!on) {
        ac_sen6x_detach();
        ac_hp_bus_close(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL);
        return gpio_set_level(AC_PIN_SEN_EN, 0);
    }
    esp_err_t err = gpio_set_level(AC_PIN_SEN_EN, 1);
    if (err != ESP_OK) return err;
    /* SEN6x datasheet Table 1: 100 ms from power-on until I2C. */
    vTaskDelay(pdMS_TO_TICKS(120));
    return ac_hp_bus_open(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL);
}

void ac_rail_hold(bool hold)
{
    const int pins[] = { AC_PIN_SEN_EN, AC_PIN_CO2_EN, AC_PIN_CO2_IO, AC_PIN_CE };
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        if (hold) gpio_hold_en(pins[i]);
        else      gpio_hold_dis(pins[i]);
    }
}
