/* Sensirion SHT40 (Seeed Grove SHT4x board): temperature and humidity.
 *
 * SHT4x datasheet: command 0xFD measures T and RH with high repeatability,
 * max 8.3 ms; the answer is 2 x (16-bit word + CRC-8, polynomial 0x31).
 *   T  = -45 + 175 * S_T  / 65535   [degC]
 *   RH =  -6 + 125 * S_RH / 65535   [%RH], cropped to 0..100
 * Idle current is a fraction of a microamp, so the sensor stays powered and is
 * read with every VOC sample - which also gives the SGP40 fresh compensation
 * values every 10 s instead of hourly ones (EDR-17).
 */
#include "ac_hal/ac_hal.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern i2c_master_bus_handle_t g_i2c;
extern uint8_t ac_sensirion_crc(const uint8_t *data, size_t n);

static i2c_master_dev_handle_t s_dev;

esp_err_t ac_sht4x_measure(float *t_c, float *rh)
{
    if (!s_dev) {
        if (!g_i2c) return ESP_ERR_INVALID_STATE;
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AC_I2C_ADDR_SHT4X,
            .scl_speed_hz = 100000,
        };
        esp_err_t err = i2c_master_bus_add_device(g_i2c, &cfg, &s_dev);
        if (err != ESP_OK) return err;
    }
    const uint8_t cmd = 0xFD;
    esp_err_t err = i2c_master_transmit(s_dev, &cmd, 1, 100);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t b[6];
    err = i2c_master_receive(s_dev, b, sizeof(b), 100);
    if (err != ESP_OK) return err;
    if (ac_sensirion_crc(&b[0], 2) != b[2] || ac_sensirion_crc(&b[3], 2) != b[5])
        return ESP_ERR_INVALID_CRC;
    float st = (float)((b[0] << 8) | b[1]);
    float srh = (float)((b[3] << 8) | b[4]);
    float h = -6.0f + 125.0f * srh / 65535.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 100.0f) h = 100.0f;
    *t_c = -45.0f + 175.0f * st / 65535.0f;
    *rh = h;
    return ESP_OK;
}
