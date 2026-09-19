/* Sensirion SEN62: PM1/2.5/4/10 (and T/RH, which this device takes from the
 * SHT40 instead - see EDR-17).
 *
 * Command IDs, execution times and scaling: SEN6x datasheet v0.92 (Dec 2025),
 * section 4.8.  Operated power-cycled: the module idles at 3.3 mA, so between
 * windows it is switched off by the Pololu switch, not idled.  Sensirion's
 * figures assume a settled module: the typical start-up to stable PM is 30 s
 * and the precision is specified on averages taken after that, so every
 * window throws its first AC_PM_SETTLE_S away and averages the rest
 * (AC_PM_MIN_WINDOW_S = 60 s: 30 s averaged, as the SEN5x reduced-power note
 * recommends).
 */
#include "ac_hal/ac_hal.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sen6x";
extern i2c_master_bus_handle_t g_hp;
extern uint8_t ac_sensirion_crc(const uint8_t *data, size_t n);

#define CMD_START_MEASUREMENT   0x0021   /*   50 ms */
#define CMD_STOP_MEASUREMENT    0x0104   /* 1400 ms */
#define CMD_READ_VALUES_SEN62   0x04A3   /*   20 ms, 6 words */
#define CMD_READ_NUMBER_CONC    0x0316   /*   20 ms, 5 words */
#define CMD_SERIAL              0xD033   /*   20 ms, 16 words */
#define CMD_READ_STATUS         0xD206   /*   20 ms, 2 words */

/* Device status bits (SEN6x datasheet, Read Device Status). */
#define ST_FAN_ERROR     (1u << 4)
#define ST_RHT_ERROR     (1u << 6)
#define ST_PM_ERROR      (1u << 11)
#define ST_FAN_WARNING   (1u << 21)

static i2c_master_dev_handle_t s_dev;
static bool s_running;

static esp_err_t attach(void)
{
    if (s_dev) return ESP_OK;
    if (!g_hp) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AC_I2C_ADDR_SEN6X,
        .scl_speed_hz = 100000,          /* datasheet: 100 kbit/s max */
    };
    return i2c_master_bus_add_device(g_hp, &cfg, &s_dev);
}

void ac_sen6x_detach(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    s_running = false;
}

static esp_err_t cmd(uint16_t c, uint32_t exec_ms)
{
    uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)c };
    esp_err_t err = i2c_master_transmit(s_dev, b, 2, 100);
    if (exec_ms) vTaskDelay(pdMS_TO_TICKS(exec_ms));
    return err;
}

static esp_err_t read_words(uint16_t *out, size_t words)
{
    uint8_t buf[48];
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

static esp_err_t read_cmd(uint16_t c, uint16_t *out, size_t words)
{
    esp_err_t err = cmd(c, 20);
    if (err != ESP_OK) return err;
    return read_words(out, words);
}

static esp_err_t power_up(void)
{
    esp_err_t err = ac_rail_sen6x(true);
    if (err == ESP_OK) err = attach();
    if (err != ESP_OK) ac_rail_sen6x(false);
    return err;
}

void ac_sen6x_power_off(void)
{
    if (s_running && s_dev) cmd(CMD_STOP_MEASUREMENT, 1400);
    ac_rail_sen6x(false);      /* also detaches and clears s_running */
}

esp_err_t ac_sen6x_measure_window(uint32_t window_s, bool keep_running,
                                  ac_sen6x_values_t *out)
{
    if (window_s < AC_PM_MIN_WINDOW_S) window_s = AC_PM_MIN_WINDOW_S;

    esp_err_t err;
    bool fresh_start = !s_running;
    if (fresh_start) {
        err = power_up();
        if (err != ESP_OK) return err;
        err = cmd(CMD_START_MEASUREMENT, 50);
        if (err != ESP_OK) { ac_sen6x_power_off(); return err; }
        s_running = true;
    } else {
        /* continuous mode: the module kept running, the bus may have been
         * lent to the Sunrise in between */
        err = ac_hp_bus_open(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL);
        if (err == ESP_OK) err = attach();
        if (err != ESP_OK) return err;
    }

    float pm[4] = { 0 };
    uint32_t n_pm = 0;
    for (uint32_t sec = 1; sec <= window_s; sec++) {
        ac_hal_wait_ms(1000);            /* VOC samples happen in here */
        if (fresh_start && sec <= AC_PM_SETTLE_S) continue;   /* not settled */
        uint16_t w[6];
        if (read_cmd(CMD_READ_VALUES_SEN62, w, 6) != ESP_OK) continue;
        /* "unknown" is 0xFFFF for the unsigned PM words */
        if (w[1] == 0xFFFF) continue;
        pm[0] += w[0] / 10.0f; pm[1] += w[1] / 10.0f;
        pm[2] += w[2] / 10.0f; pm[3] += w[3] / 10.0f;
        n_pm++;
    }

    uint16_t nc[5];
    bool have_nc = read_cmd(CMD_READ_NUMBER_CONC, nc, 5) == ESP_OK;
    uint16_t st[2] = { 0, 0 };
    read_cmd(CMD_READ_STATUS, st, 2);
    uint32_t status = ((uint32_t)st[0] << 16) | st[1];

    if (keep_running) {
        /* leave it measuring; hand the bus back */
        ac_sen6x_detach();
        s_running = true;
        ac_hp_bus_close(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL);
    } else {
        ac_sen6x_power_off();
    }

    if (n_pm == 0) return ESP_ERR_TIMEOUT;
    memset(out, 0, sizeof(*out));
    out->pm1 = pm[0] / n_pm;  out->pm25 = pm[1] / n_pm;
    out->pm4 = pm[2] / n_pm;  out->pm10 = pm[3] / n_pm;
    out->n05 = have_nc && nc[0] != 0xFFFF ? nc[0] / 10.0f : -1.0f;
    out->n10 = have_nc && nc[1] != 0xFFFF ? nc[1] / 10.0f : -1.0f;
    out->status = status;
    out->samples = n_pm;

    if (status & (ST_FAN_WARNING | ST_FAN_ERROR))
        ESP_LOGW(TAG, "fan %s (status 0x%08lx)",
                 (status & ST_FAN_ERROR) ? "error" : "speed warning",
                 (unsigned long)status);
    ESP_LOGI(TAG, "window %lus: PM2.5 %.1f ug/m3 averaged over %lu s",
             (unsigned long)window_s, (double)out->pm25, (unsigned long)n_pm);
    /* A fan or laser fault makes the PM numbers meaningless; say so. */
    if (status & (ST_FAN_ERROR | ST_PM_ERROR)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t ac_sen6x_serial(char *out, size_t n)
{
    bool was_running = s_running;
    esp_err_t err = was_running ? ac_hp_bus_open(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL)
                                : power_up();
    if (err == ESP_OK) err = attach();
    if (err != ESP_OK) { if (!was_running) ac_rail_sen6x(false); return err; }
    uint16_t w[16];
    err = read_cmd(CMD_SERIAL, w, 16);
    if (!was_running) {
        ac_rail_sen6x(false);
    } else {
        ac_sen6x_detach();
        s_running = true;
        ac_hp_bus_close(AC_PIN_SEN_SDA, AC_PIN_SEN_SCL);
    }
    if (err != ESP_OK) return err;
    size_t k = 0;
    for (int i = 0; i < 16 && k + 2 < n; i++) {
        char a = (char)(w[i] >> 8), b = (char)w[i];
        if (!a) break;
        out[k++] = a;
        if (!b) break;
        out[k++] = b;
    }
    out[k] = '\0';
    return ESP_OK;
}
