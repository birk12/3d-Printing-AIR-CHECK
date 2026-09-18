/* MAX17048 fuel gauge on the Adafruit ESP32-C6 Feather, I2C 0x36.
 *
 * A fuel gauge rather than a resistor divider because a LiPo's voltage says
 * almost nothing about its remaining charge over the flat middle of the
 * discharge curve, and a device that claims six months of runtime has to be
 * honest about where it actually is.
 */
#include "ac_hal/ac_hal.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

extern i2c_master_bus_handle_t g_gauge;
static i2c_master_dev_handle_t s_dev;

#define REG_VCELL   0x02
#define REG_SOC     0x04
#define REG_MODE    0x06
#define REG_CRATE   0x16
#define REG_HIBRT   0x0A

static esp_err_t read_reg(uint8_t reg, uint16_t *out)
{
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, rx, 2, 100);
    if (err != ESP_OK) return err;
    *out = (uint16_t)((rx[0] << 8) | rx[1]);
    return ESP_OK;
}

static esp_err_t write_reg(uint8_t reg, uint16_t v)
{
    uint8_t tx[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_master_transmit(s_dev, tx, 3, 100);
}

static esp_err_t read_all(float *volts, float *percent, bool *charging)
{
    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AC_I2C_ADDR_MAX17048,
            .scl_speed_hz = 100000,
        };
        esp_err_t err = i2c_master_bus_add_device(g_gauge, &cfg, &s_dev);
        if (err != ESP_OK) return err;
    }
    uint16_t v = 0, soc = 0, crate = 0;
    esp_err_t err = read_reg(REG_VCELL, &v);
    if (err != ESP_OK) return err;
    err = read_reg(REG_SOC, &soc);
    if (err != ESP_OK) return err;
    read_reg(REG_CRATE, &crate);

    if (volts) *volts = (float)v * 78.125e-6f;     /* 78.125 uV per LSB */
    if (percent) {
        float p = (float)soc / 256.0f;
        if (p > 100.0f) p = 100.0f;
        *percent = p;
    }
    if (charging) {
        /* CRATE is signed, 0.208 % of capacity per hour per LSB.  A positive
         * rate means the pack is gaining charge, which only happens on USB. */
        int16_t signed_rate = (int16_t)crate;
        *charging = signed_rate > 0;
    }
    return ESP_OK;
}

/* The Feather's gauge bus only has pull-ups while GPIO20 is high (see
 * ac_hal.c), so every access is bracketed by switching it on and off again.
 * The MAX17048 itself runs from VBAT and keeps tracking the cell meanwhile. */
esp_err_t ac_battery_read(float *volts, float *percent, bool *charging)
{
    esp_err_t err = ac_gauge_bus(true);
    if (err == ESP_OK) err = read_all(volts, percent, charging);
    ac_gauge_bus(false);
    return err;
}

/* Hibernate drops the gauge from ~23 uA to ~3 uA and slows its update to once
 * every 45 s, which is far more often than this device changes state anyway. */
esp_err_t ac_battery_hibernate(bool on)
{
    esp_err_t err = ac_gauge_bus(true);
    if (err != ESP_OK) { ac_gauge_bus(false); return err; }
    if (!s_dev) {
        float v, p; bool c;
        err = read_all(&v, &p, &c);          /* attaches the device */
        if (err != ESP_OK) { ac_gauge_bus(false); return err; }
    }
    err = write_reg(REG_HIBRT, on ? 0xFFFF : 0x0000);
    ac_gauge_bus(false);
    return err;
}


void ac_battery_detach(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}
