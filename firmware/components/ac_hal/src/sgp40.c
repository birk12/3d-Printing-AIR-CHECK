/* Sensirion SGP40 VOC sensor.
 *
 * Command codes, timings and the CRC polynomial: SGP40 datasheet v1.2,
 * section 4.7.  Low-power sequence: Sensirion's gas-index-algorithm examples -
 * heater off between samples, a discarded measurement 170 ms ahead of the real
 * one to bring the hotplate up.  That is what turns 2.6 mA continuous into the
 * 0.2 mW at a 10 s interval that the energy model is built on.
 *
 * The VOC Index itself is Sensirion's algorithm, vendored under
 * components/sensirion_gas_index.  It is a *relative* indoor air quality
 * indicator: 100 means "the average of this room over the last 24 hours".  It
 * does not identify or quantify any individual chemical, and nothing in this
 * firmware pretends otherwise.
 */
#include "ac_hal/ac_hal.h"
#include "sensirion_gas_index_algorithm.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sgp40";
extern i2c_master_bus_handle_t g_i2c;

static i2c_master_dev_handle_t s_dev;
static GasIndexAlgorithmParams s_voc;
static bool s_algo_ready;

uint8_t ac_sensirion_crc(const uint8_t *data, size_t n)
{
    /* CRC-8, polynomial 0x31, init 0xFF - common to every Sensirion part. */
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

static esp_err_t tx(const uint8_t *buf, size_t n)
{
    return i2c_master_transmit(s_dev, buf, n, 100);
}

static esp_err_t rx_words(uint16_t *out, size_t words)
{
    uint8_t buf[9];
    if (words * 3 > sizeof(buf)) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = i2c_master_receive(s_dev, buf, words * 3, 100);
    if (err != ESP_OK) return err;
    for (size_t i = 0; i < words; i++) {
        if (ac_sensirion_crc(&buf[i * 3], 2) != buf[i * 3 + 2])
            return ESP_ERR_INVALID_CRC;
        out[i] = (uint16_t)((buf[i * 3] << 8) | buf[i * 3 + 1]);
    }
    return ESP_OK;
}

esp_err_t ac_sgp40_init(uint32_t sampling_interval_s)
{
    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AC_I2C_ADDR_SGP40,
            .scl_speed_hz = 100000,     /* the SPS30 is not on this bus, but
                                         * 100 kHz keeps the cable benign */
        };
        esp_err_t err = i2c_master_bus_add_device(g_i2c, &cfg, &s_dev);
        if (err != ESP_OK) return err;
    }
    /* The algorithm is validated at 1 s and 10 s.  Anything else is outside
     * Sensirion's tested range, so the engine's config validator already
     * clamps the interval to 1..10 s. */
    float interval = (float)(sampling_interval_s ? sampling_interval_s : 1);
    GasIndexAlgorithm_init_with_sampling_interval(&s_voc,
        GasIndexAlgorithm_ALGORITHM_TYPE_VOC, interval);
    s_algo_ready = true;
    ESP_LOGI(TAG, "VOC index algorithm at a %.0f s sampling interval",
             (double)interval);
    return ESP_OK;
}

esp_err_t ac_sgp40_self_test(void)
{
    const uint8_t cmd[2] = { 0x28, 0x0E };
    esp_err_t err = tx(cmd, sizeof(cmd));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(320));
    uint16_t w;
    err = rx_words(&w, 1);
    if (err != ESP_OK) return err;
    /* 0xD400 = all tests passed, 0x4B00 = at least one failed */
    return (w == 0xD400) ? ESP_OK : ESP_FAIL;
}

static esp_err_t measure_raw(float t_c, float rh, uint16_t *raw)
{
    /* Humidity compensation, datasheet 3.1: RH in ticks of 65535/100 %,
     * temperature in ticks of 65535/175 offset by -45 degC. */
    if (rh < 0.0f) rh = 50.0f;
    if (t_c < -45.0f) t_c = 25.0f;
    uint16_t rh_ticks = (uint16_t)((rh * 65535.0f) / 100.0f + 0.5f);
    uint16_t t_ticks = (uint16_t)(((t_c + 45.0f) * 65535.0f) / 175.0f + 0.5f);

    uint8_t cmd[8];
    cmd[0] = 0x26; cmd[1] = 0x0F;
    cmd[2] = (uint8_t)(rh_ticks >> 8); cmd[3] = (uint8_t)rh_ticks;
    cmd[4] = ac_sensirion_crc(&cmd[2], 2);
    cmd[5] = (uint8_t)(t_ticks >> 8); cmd[6] = (uint8_t)t_ticks;
    cmd[7] = ac_sensirion_crc(&cmd[5], 2);

    esp_err_t err = tx(cmd, sizeof(cmd));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(35));     /* datasheet: 30 ms max */
    return rx_words(raw, 1);
}

static esp_err_t heater_off(void)
{
    const uint8_t cmd[2] = { 0x36, 0x15 };
    return tx(cmd, sizeof(cmd));
}

esp_err_t ac_sgp40_measure(float t_c, float rh, int32_t *raw_out,
                           int32_t *index_out)
{
    uint16_t raw = 0;
    /* first, discarded measurement: this is what turns the hotplate on */
    esp_err_t err = measure_raw(t_c, rh, &raw);
    if (err != ESP_OK) { heater_off(); return err; }
    vTaskDelay(pdMS_TO_TICKS(135));    /* 170 ms total including the 35 above */
    err = measure_raw(t_c, rh, &raw);
    heater_off();
    if (err != ESP_OK) return err;

    if (raw_out) *raw_out = raw;
    if (index_out) {
        if (!s_algo_ready) return ESP_ERR_INVALID_STATE;
        int32_t idx = 0;
        GasIndexAlgorithm_process(&s_voc, (int32_t)raw, &idx);
        *index_out = idx;
    }
    return ESP_OK;
}

esp_err_t ac_sgp40_serial(uint64_t *out)
{
    const uint8_t cmd[2] = { 0x36, 0x82 };
    esp_err_t err = tx(cmd, sizeof(cmd));
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(2));
    uint16_t w[3];
    err = rx_words(w, 3);
    if (err != ESP_OK) return err;
    *out = ((uint64_t)w[0] << 32) | ((uint64_t)w[1] << 16) | w[2];
    return ESP_OK;
}

void ac_sgp40_detach(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}
