/* Sensirion SCD41 CO2 / temperature / humidity sensor.
 *
 * Operated in power-cycled single-shot mode: wake, measure_single_shot, read,
 * power_down.  Sensirion's low-power application note gives 250 uA at a 10 min
 * cadence and 43 uA at 1 h in that mode, which is what makes a six month
 * battery target reachable at all.
 *
 * The trade-off is real and is documented in docs/CALIBRATION.md: automatic
 * self calibration is *not* available when the sensor is power cycled, so the
 * firmware implements the equivalent itself over a seven day window.
 */
#include "ac_hal/ac_hal.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "scd41";
extern i2c_master_bus_handle_t g_i2c;
extern uint8_t ac_sensirion_crc(const uint8_t *data, size_t n);

static i2c_master_dev_handle_t s_dev;
static bool s_discard_next = true;   /* the first shot after a power cycle */

static esp_err_t cmd(uint16_t c)
{
    uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)c };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

static esp_err_t cmd_arg(uint16_t c, uint16_t arg)
{
    uint8_t b[5] = { (uint8_t)(c >> 8), (uint8_t)c,
                     (uint8_t)(arg >> 8), (uint8_t)arg, 0 };
    b[4] = ac_sensirion_crc(&b[2], 2);
    return i2c_master_transmit(s_dev, b, 5, 100);
}

static esp_err_t read_words(uint16_t *out, size_t words)
{
    uint8_t buf[9];
    if (words * 3 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = i2c_master_receive(s_dev, buf, words * 3, 200);
    if (err != ESP_OK) return err;
    for (size_t i = 0; i < words; i++) {
        if (ac_sensirion_crc(&buf[i * 3], 2) != buf[i * 3 + 2])
            return ESP_ERR_INVALID_CRC;
        out[i] = (uint16_t)((buf[i * 3] << 8) | buf[i * 3 + 1]);
    }
    return ESP_OK;
}

esp_err_t ac_scd41_init(void)
{
    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AC_I2C_ADDR_SCD41,
            .scl_speed_hz = 100000,
        };
        esp_err_t err = i2c_master_bus_add_device(g_i2c, &cfg, &s_dev);
        if (err != ESP_OK) return err;
    }
    /* stop_periodic_measurement, in case we came back from a watchdog reset
     * while the sensor was still running */
    cmd(0x3F86);
    vTaskDelay(pdMS_TO_TICKS(500));
    s_discard_next = true;
    return ESP_OK;
}

esp_err_t ac_scd41_wake_up(void)
{
    /* The wake_up command is not acknowledged, so a NACK here is expected. */
    cmd(0x36F6);
    vTaskDelay(pdMS_TO_TICKS(30));
    return ESP_OK;
}

esp_err_t ac_scd41_power_down(void)
{
    return cmd(0x36E0);
}

esp_err_t ac_scd41_single_shot(float *co2, float *t, float *rh)
{
    esp_err_t err = ac_scd41_wake_up();
    if (err != ESP_OK) return err;

    for (int attempt = 0; attempt < (s_discard_next ? 2 : 1); attempt++) {
        err = cmd(0x219D);                       /* measure_single_shot */
        if (err != ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(5100));         /* datasheet: 5000 ms max */
        err = cmd(0xEC05);                       /* read_measurement */
        if (err != ESP_OK) return err;
        uint16_t w[3];
        err = read_words(w, 3);
        if (err != ESP_OK) return err;
        if (attempt == 0 && s_discard_next) continue;
        *co2 = (float)w[0];
        *t = -45.0f + 175.0f * (float)w[1] / 65535.0f;
        *rh = 100.0f * (float)w[2] / 65535.0f;
    }
    s_discard_next = false;
    ac_scd41_power_down();
    return ESP_OK;
}

esp_err_t ac_scd41_set_temperature_offset(float c)
{
    /* The offset compensates self-heating inside *this* enclosure.  The
     * default 4 degC assumes the sensor sits next to a warm board; ours sits
     * in the ventilated sensor bay below the electronics, so the value is a
     * configuration item and not a constant. */
    uint16_t ticks = (uint16_t)(c * 65535.0f / 175.0f + 0.5f);
    esp_err_t err = cmd_arg(0x241D, ticks);
    vTaskDelay(pdMS_TO_TICKS(1));
    return err;
}

esp_err_t ac_scd41_forced_recalibration(uint16_t target_ppm, int16_t *correction)
{
    ESP_LOGW(TAG, "forced recalibration to %u ppm", target_ppm);
    esp_err_t err = cmd(0x3F86);       /* stop periodic measurement */
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(500));
    err = cmd_arg(0x362F, target_ppm);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(400));
    uint16_t w;
    err = read_words(&w, 1);
    if (err != ESP_OK) return err;
    if (w == 0xFFFF) return ESP_ERR_INVALID_STATE;   /* FRC failed */
    if (correction) *correction = (int16_t)((int32_t)w - 0x8000);
    s_discard_next = true;
    return ESP_OK;
}

esp_err_t ac_scd41_serial(uint64_t *out)
{
    esp_err_t err = cmd(0x3682);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(2));
    uint16_t w[3];
    err = read_words(w, 3);
    if (err != ESP_OK) return err;
    *out = ((uint64_t)w[0] << 32) | ((uint64_t)w[1] << 16) | w[2];
    return ESP_OK;
}

void ac_scd41_detach(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}
