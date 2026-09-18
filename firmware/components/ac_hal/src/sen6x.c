/* Sensirion SEN63C: PM1/2.5/4/10, CO2, temperature and humidity in one module.
 *
 * Command IDs, execution times and scaling: SEN6x datasheet v0.5 (Oct 2024),
 * section 5, and Sensirion's generated embedded-i2c-sen63c driver, which also
 * has the SEN63C-specific "read measured values" command (0x0471) that the
 * datasheet revision does not list yet.
 *
 * Operated power-cycled: the module idles at 3.3 mA, more than everything else
 * in the device together, so between windows it is not idled but switched
 * off.  Two consequences, both handled here:
 *  - CO2 reads 0x7FFF ("unknown") for the first 22..24 s of every measurement,
 *    so a window shorter than AC_PM_MIN_WINDOW_S returns no CO2 at all;
 *  - PM needs ~30 s (typ) to settle, so only the tail of the window is
 *    averaged.
 * Whether the module's CO2 automatic self calibration still converges when it
 * only runs 40 s an hour is not documented.  docs/CALIBRATION.md has the
 * fallback (forced recalibration outdoors), and TESTING.md has the check.
 */
#include "ac_hal/ac_hal.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sen6x";
extern i2c_master_bus_handle_t g_sen;
extern uint8_t ac_sensirion_crc(const uint8_t *data, size_t n);

#define CMD_START_MEASUREMENT   0x0021   /*   50 ms */
#define CMD_STOP_MEASUREMENT    0x0104   /* 1000 ms datasheet, 1400 ms driver */
#define CMD_DATA_READY          0x0202   /*   20 ms */
#define CMD_READ_NUMBER_CONC    0x0316   /*   20 ms, 5 words */
#define CMD_READ_VALUES_SEN63C  0x0471   /*   20 ms, 7 words */
#define CMD_FORCED_RECAL        0x6707   /*  500 ms, idle only */
#define CMD_ASC                 0x6711   /*   20 ms, idle only */
#define CMD_SERIAL              0xD033   /*   20 ms, 16 words */
#define CMD_READ_STATUS         0xD206   /*   20 ms, 2 words */

/* Device status bits, from the generated driver's sen63c_device_status. */
#define ST_FAN_ERROR     (1u << 4)
#define ST_RHT_ERROR     (1u << 6)
#define ST_CO2_2_ERROR   (1u << 9)
#define ST_PM_ERROR      (1u << 11)
#define ST_CO2_1_ERROR   (1u << 12)
#define ST_FAN_WARNING   (1u << 21)

static i2c_master_dev_handle_t s_dev;
static bool s_running;

static esp_err_t attach(void)
{
    if (s_dev) return ESP_OK;
    if (!g_sen) return ESP_ERR_INVALID_STATE;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AC_I2C_ADDR_SEN6X,
        .scl_speed_hz = 100000,          /* datasheet 5.3: 100 kbit/s max */
    };
    return i2c_master_bus_add_device(g_sen, &cfg, &s_dev);
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

static esp_err_t cmd_arg(uint16_t c, uint16_t arg, uint32_t exec_ms)
{
    uint8_t b[5] = { (uint8_t)(c >> 8), (uint8_t)c,
                     (uint8_t)(arg >> 8), (uint8_t)arg, 0 };
    b[4] = ac_sensirion_crc(&b[2], 2);
    esp_err_t err = i2c_master_transmit(s_dev, b, 5, 100);
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

/* Power up and attach, without starting a measurement. */
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
    /* Average PM over the tail: at least the 30 s start-up behind us, and at
     * least 5 samples.  CO2, T and RH: the last valid reading, because the
     * CO2 output is still converging (tau63 = 20 s) and an average would drag
     * the start-up transient in. */
    uint32_t settle_s = window_s - window_s / 3;
    if (settle_s < 30) settle_s = 30;
    if (settle_s > window_s - 5) settle_s = window_s - 5;

    esp_err_t err;
    bool fresh_start = !s_running;
    if (fresh_start) {
        err = power_up();
        if (err != ESP_OK) return err;
        err = cmd(CMD_START_MEASUREMENT, 50);
        if (err != ESP_OK) { ac_sen6x_power_off(); return err; }
        s_running = true;
    }

    float pm[4] = { 0 };
    uint32_t n_pm = 0;
    float co2 = -1.0f, t = -273.0f, rh = -1.0f;
    for (uint32_t sec = 1; sec <= window_s; sec++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint16_t w[7];
        if (read_cmd(CMD_READ_VALUES_SEN63C, w, 7) != ESP_OK) continue;
        /* "unknown" is 0xFFFF for the unsigned PM words and 0x7FFF for the
         * signed RH, T and CO2 words */
        if ((!fresh_start || sec > settle_s) && w[1] != 0xFFFF) {
            pm[0] += w[0] / 10.0f; pm[1] += w[1] / 10.0f;
            pm[2] += w[2] / 10.0f; pm[3] += w[3] / 10.0f;
            n_pm++;
        }
        if (w[4] != 0x7FFF) rh = (int16_t)w[4] / 100.0f;
        if (w[5] != 0x7FFF) t = (int16_t)w[5] / 200.0f;
        if (w[6] != 0x7FFF && (int16_t)w[6] > 0) co2 = (float)(int16_t)w[6];
    }

    uint16_t nc[5];
    bool have_nc = read_cmd(CMD_READ_NUMBER_CONC, nc, 5) == ESP_OK;
    uint16_t st[2] = { 0, 0 };
    read_cmd(CMD_READ_STATUS, st, 2);
    uint32_t status = ((uint32_t)st[0] << 16) | st[1];

    if (!keep_running) ac_sen6x_power_off();

    if (n_pm == 0) return ESP_ERR_TIMEOUT;
    memset(out, 0, sizeof(*out));
    out->pm1 = pm[0] / n_pm;  out->pm25 = pm[1] / n_pm;
    out->pm4 = pm[2] / n_pm;  out->pm10 = pm[3] / n_pm;
    out->n05 = have_nc && nc[0] != 0xFFFF ? nc[0] / 10.0f : -1.0f;
    out->n10 = have_nc && nc[1] != 0xFFFF ? nc[1] / 10.0f : -1.0f;
    out->co2 = (status & (ST_CO2_1_ERROR | ST_CO2_2_ERROR)) ? -1.0f : co2;
    out->temperature = (status & ST_RHT_ERROR) ? -273.0f : t;
    out->humidity = (status & ST_RHT_ERROR) ? -1.0f : rh;
    out->status = status;

    if (status & (ST_FAN_WARNING | ST_FAN_ERROR))
        ESP_LOGW(TAG, "fan %s (status 0x%08lx)",
                 (status & ST_FAN_ERROR) ? "error" : "speed warning",
                 (unsigned long)status);
    ESP_LOGI(TAG, "window %lus: PM2.5 %.1f ug/m3 (%lu samples), CO2 %.0f ppm, "
             "%.1f C, %.0f %%RH", (unsigned long)window_s, (double)out->pm25,
             (unsigned long)n_pm, (double)out->co2, (double)out->temperature,
             (double)out->humidity);
    /* A fan or laser fault makes the PM numbers meaningless; say so. */
    if (status & (ST_FAN_ERROR | ST_PM_ERROR)) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t ac_sen6x_serial(char *out, size_t n)
{
    bool was_running = s_running;
    if (!was_running) {
        esp_err_t err = power_up();
        if (err != ESP_OK) return err;
    }
    uint16_t w[16];
    esp_err_t err = read_cmd(CMD_SERIAL, w, 16);
    if (!was_running) ac_rail_sen6x(false);
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

esp_err_t ac_sen6x_set_asc(bool on)
{
    if (s_running) return ESP_ERR_INVALID_STATE;     /* idle-only command */
    esp_err_t err = power_up();
    if (err != ESP_OK) return err;
    uint16_t w;
    err = read_cmd(CMD_ASC, &w, 1);
    /* The flag lives in the low byte; the high byte is padding. */
    if (err == ESP_OK && ((w & 0xFF) != 0) != on) {
        ESP_LOGI(TAG, "CO2 self calibration -> %s", on ? "on" : "off");
        err = cmd_arg(CMD_ASC, on ? 1 : 0, 20);
    }
    ac_rail_sen6x(false);
    return err;
}

esp_err_t ac_sen6x_forced_recalibration(uint16_t target_ppm, int16_t *correction)
{
    /* Sensirion's procedure (SCD4x, which the SEN63C's FRC defers to): run in
     * the reference air for at least three minutes, stop, then recalibrate.
     * So if a window left the module running, stop it without powering down;
     * the driver wants 600 ms after a stop, the stop command itself takes
     * 1400 ms.  From cold, at least 1000 ms after power-on. */
    ESP_LOGW(TAG, "forced CO2 recalibration to %u ppm", target_ppm);
    esp_err_t err;
    if (s_running && s_dev) {
        err = cmd(CMD_STOP_MEASUREMENT, 1400);
        s_running = false;
    } else {
        err = power_up();
        if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (err != ESP_OK) { ac_rail_sen6x(false); return err; }
    err = cmd_arg(CMD_FORCED_RECAL, target_ppm, 500);
    uint16_t w = 0xFFFF;
    if (err == ESP_OK) err = read_words(&w, 1);
    ac_rail_sen6x(false);
    if (err != ESP_OK) return err;
    if (w == 0xFFFF) return ESP_ERR_INVALID_STATE;   /* FRC failed */
    if (correction) *correction = (int16_t)((int32_t)w - 0x8000);
    return ESP_OK;
}
