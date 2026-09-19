/* Senseair Sunrise 006-0-0008: NDIR CO2, +-(30 ppm + 3 %).
 *
 * Everything here is from Senseair's own documents:
 *   TDE5531 "Sunrise/Sunlight I2C" rev 18 - register map, wake-up, the
 *           single-measurement sequence with host-held ABC state (3.4),
 *           calibration commands (0x7C05/0x7C06);
 *   TDE7318 "Sunrise/Sunlight integration" - low-power wiring (no signals or
 *           pull-ups while EN is low), 32 samples and IIR off for long
 *           measurement periods, T_sample <= 200 ms for 006-0-0008.
 *
 * Operation: the sensor is fully off between measurements - EN low, and VDDIO
 * plus both pull-ups hang off GPIO18, which is driven low - so it costs
 * ~0.2 uA of shutdown current on VBB and nothing else.  Its ABC (automatic
 * baseline correction) keeps working because the host reads the ABC state
 * registers after every measurement, adds the hours that have passed, and
 * writes them back before the next one.  Senseair: "these registers must be
 * read from the sensor after each measurement and written back to the sensor
 * after each power on (enable) before a new measurement is trigged."
 */
#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "sunrise";
extern i2c_master_bus_handle_t g_hp;

/* read-only */
#define R_ERROR_STATUS   0x00   /* 2 bytes */
#define R_CO2_FILT_PC    0x06   /* filtered, pressure compensated */
#define R_CHIP_TEMP      0x08
#define R_MEAS_COUNT     0x0D
/* read/write */
#define R_CAL_STATUS     0x81
#define R_CAL_COMMAND    0x82   /* 2 bytes */
#define R_CAL_TARGET     0x84   /* 2 bytes */
#define R_MEAS_MODE      0x95   /* EE */
#define R_NUM_SAMPLES    0x98   /* EE, 2 bytes */
#define R_ABC_PERIOD     0x9A   /* EE, 2 bytes, hours */
#define R_ABC_TARGET     0x9E   /* EE, 2 bytes, ppm */
#define R_SCR            0xA3
#define R_METER_CONTROL  0xA5   /* EE */
#define R_START_SINGLE   0xC3   /* mirror of 0x93; state follows at 0xC4.. */
#define R_STATE          0xC4   /* ABC time, ABC par0-3, filter par0-6 */
#define R_PRESSURE       0xDC   /* 2 bytes, 0.1 hPa */

#define CMD_TARGET_CAL   0x7C05

/* Meter control: nRDY disabled (not wired), ABC per config, static and
 * dynamic IIR off (TDE7318: for periods > 1 min "disable both IIR
 * filtrations and increase the number of samples"), pressure compensation on,
 * nRDY invert at its default. */
#define MC_NRDY_OFF      (1u << 0)
#define MC_ABC_OFF       (1u << 1)
#define MC_SIIR_OFF      (1u << 2)
#define MC_DIIR_OFF      (1u << 3)
#define MC_PC_OFF        (1u << 4)
#define MC_NRDY_NOINV    (1u << 5)

#define SAMPLES          32
#define ABC_PERIOD_H     180    /* Senseair default, 7.5 days */
#define ABC_TARGET_PPM   425    /* outdoor background, 2026 */
#define T_SAMPLE_MAX_MS  300    /* TDE7318: < 200 ms for -0008, 300 ms worst */
#define EE_WRITE_MS      150    /* TDE5531: < 107 ms */

/* error status, low byte */
#define ES_NO_MEASUREMENT (1u << 7)

static i2c_master_dev_handle_t s_dev;

void ac_sunrise_detach(void)
{
    if (s_dev) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
}

static esp_err_t power_on(void)
{
    /* VDDIO and the pull-ups first, then EN: TDE5531 3.4 "drive EN high,
     * wait for minimum 35 ms". */
    gpio_set_level(AC_PIN_CO2_IO, 1);
    gpio_set_level(AC_PIN_CO2_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(40));
    esp_err_t err = ac_hp_bus_open(AC_PIN_CO2_SDA, AC_PIN_CO2_SCL);
    if (err != ESP_OK) return err;
    if (!s_dev) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AC_I2C_ADDR_SUNRISE,
            .scl_speed_hz = 100000,
            .scl_wait_us = 20000,   /* the sensor stretches SCL while it works */
        };
        err = i2c_master_bus_add_device(g_hp, &cfg, &s_dev);
    }
    return err;
}

static void power_off(void)
{
    ac_sunrise_detach();
    /* bus pins become inputs before VDDIO and the pull-ups go away */
    ac_hp_bus_close(AC_PIN_CO2_SDA, AC_PIN_CO2_SCL);
    gpio_set_level(AC_PIN_CO2_EN, 0);
    gpio_set_level(AC_PIN_CO2_IO, 0);
}

/* The sensor sleeps between transactions and wakes on the falling SDA edge of
 * an address byte it will not acknowledge (TDE5531 2.1.1).  The real
 * transaction has to follow within 15 ms. */
static void wake(void)
{
    (void)i2c_master_probe(g_hp, AC_I2C_ADDR_SUNRISE, 20);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    wake();
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 200);
}

static esp_err_t wr(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t b[1 + AC_SUNRISE_STATE_LEN + 4];
    if (n + 1 > sizeof(b)) return ESP_ERR_INVALID_SIZE;
    b[0] = reg;
    memcpy(&b[1], data, n);
    wake();
    return i2c_master_transmit(s_dev, b, n + 1, 200);
}

static esp_err_t wr16(uint8_t reg, uint16_t v)
{
    uint8_t d[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return wr(reg, d, 2);
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int16_t pressure_reg(float hpa)
{
    if (hpa < 700.0f) hpa = 700.0f;
    if (hpa > 1200.0f) hpa = 1200.0f;
    return (int16_t)lrintf(hpa * 10.0f);      /* unit 0.1 hPa */
}

esp_err_t ac_sunrise_setup(bool abc)
{
    esp_err_t err = power_on();
    if (err != ESP_OK) { power_off(); return err; }

    uint8_t mode = 0, mc = 0, b2[2];
    uint16_t samples = 0, period = 0, target = 0;
    err = rd(R_MEAS_MODE, &mode, 1);
    if (err == ESP_OK) err = rd(R_NUM_SAMPLES, b2, 2), samples = be16(b2);
    if (err == ESP_OK) err = rd(R_ABC_PERIOD, b2, 2), period = be16(b2);
    if (err == ESP_OK) err = rd(R_ABC_TARGET, b2, 2), target = be16(b2);
    if (err == ESP_OK) err = rd(R_METER_CONTROL, &mc, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "not answering: %s", esp_err_to_name(err));
        power_off();
        return err;
    }

    uint8_t want_mc = MC_NRDY_OFF | MC_SIIR_OFF | MC_DIIR_OFF | MC_NRDY_NOINV |
                      (abc ? 0 : MC_ABC_OFF);
    int writes = 0;
    /* Each of these is an EEPROM write (10 000 in the sensor's lifetime), so
     * only what differs is written - normally nothing after the first boot. */
    if (mode != 1) {
        uint8_t one = 1;
        err = wr(R_MEAS_MODE, &one, 1); writes++;
        vTaskDelay(pdMS_TO_TICKS(EE_WRITE_MS));
    }
    if (err == ESP_OK && samples != SAMPLES) {
        err = wr16(R_NUM_SAMPLES, SAMPLES); writes++;
        vTaskDelay(pdMS_TO_TICKS(EE_WRITE_MS));
    }
    if (err == ESP_OK && period != ABC_PERIOD_H) {
        err = wr16(R_ABC_PERIOD, ABC_PERIOD_H); writes++;
        vTaskDelay(pdMS_TO_TICKS(EE_WRITE_MS));
    }
    if (err == ESP_OK && target != ABC_TARGET_PPM) {
        err = wr16(R_ABC_TARGET, ABC_TARGET_PPM); writes++;
        vTaskDelay(pdMS_TO_TICKS(EE_WRITE_MS));
    }
    if (err == ESP_OK && mc != want_mc) {
        err = wr(R_METER_CONTROL, &want_mc, 1); writes++;
        vTaskDelay(pdMS_TO_TICKS(EE_WRITE_MS));
    }
    if (err == ESP_OK && writes) {
        /* "A system reset is required after changing configuration" */
        uint8_t ff = 0xFF;
        wr(R_SCR, &ff, 1);
        ESP_LOGW(TAG, "EEPROM configuration updated (%d register(s)), sensor reset",
                 writes);
        vTaskDelay(pdMS_TO_TICKS(100));
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "configuration OK: single mode, %u samples, ABC %s (%u h, %u ppm)",
                 SAMPLES, abc ? "on" : "off", ABC_PERIOD_H, ABC_TARGET_PPM);
    }
    power_off();
    return err;
}

/* Start one measurement with the saved state, wait it out, read the result
 * and the new state.  `cal_target` != 0 turns it into a target calibration. */
static esp_err_t run(ac_sunrise_state_t *st, float pressure_hpa, uint16_t cal_target,
                     ac_sunrise_values_t *out)
{
    esp_err_t err = power_on();
    if (err != ESP_OK) { power_off(); return err; }

    if (cal_target) {
        uint8_t zero = 0;
        err = wr(R_CAL_STATUS, &zero, 1);                     /* clear first */
        if (err == ESP_OK) err = wr16(R_CAL_TARGET, cal_target);
        if (err == ESP_OK) err = wr16(R_CAL_COMMAND, CMD_TARGET_CAL);
    }

    int16_t p = pressure_reg(pressure_hpa);
    if (err == ESP_OK && st->valid) {
        /* ABC Time counts hours; add what has passed since the last run */
        uint32_t add_h = st->abc_ms / 3600000u;
        st->abc_ms -= add_h * 3600000u;
        uint32_t h = be16(&st->regs[0]) + add_h;
        if (h > 0xFFFE) h = 0xFFFE;
        st->regs[0] = (uint8_t)(h >> 8);
        st->regs[1] = (uint8_t)h;
        uint8_t blk[1 + AC_SUNRISE_STATE_LEN + 2];
        blk[0] = 0x01;                                          /* start */
        memcpy(&blk[1], st->regs, AC_SUNRISE_STATE_LEN);
        blk[1 + AC_SUNRISE_STATE_LEN] = (uint8_t)((uint16_t)p >> 8);
        blk[2 + AC_SUNRISE_STATE_LEN] = (uint8_t)p;
        err = wr(R_START_SINGLE, blk, sizeof(blk));
    } else if (err == ESP_OK) {
        /* No state yet: never write zeros into it (TDE5531 3.4, 3.2) -
         * pressure on its own, then just the start command. */
        uint8_t pp[2] = { (uint8_t)((uint16_t)p >> 8), (uint8_t)p };
        err = wr(R_PRESSURE, pp, 2);
        uint8_t one = 1;
        if (err == ESP_OK) err = wr(R_START_SINGLE, &one, 1);
    }
    if (err != ESP_OK) { power_off(); return err; }

    /* 32 samples at up to 300 ms each; the sensor sleeps in between and the
     * C6 can sleep too - there is no nRDY line to wait on. */
    ac_hal_wait_ms(SAMPLES * T_SAMPLE_MAX_MS + 200);   /* VOC continues */

    uint8_t r[10];
    for (int tries = 0; tries < 3; tries++) {
        err = rd(R_ERROR_STATUS, r, sizeof(r));
        if (err == ESP_OK && !(r[1] & ES_NO_MEASUREMENT)) break;
        ac_hal_wait_ms(1000);
    }
    uint8_t state[AC_SUNRISE_STATE_LEN];
    esp_err_t serr = err == ESP_OK ? rd(R_STATE, state, sizeof(state)) : err;
    uint8_t cal = 0;
    if (cal_target && err == ESP_OK) rd(R_CAL_STATUS, &cal, 1);
    power_off();

    if (err != ESP_OK) return err;
    if (serr == ESP_OK) {
        memcpy(st->regs, state, sizeof(state));
        st->valid = true;
    }
    out->error_status = r[1];
    out->co2 = (float)(int16_t)be16(&r[R_CO2_FILT_PC]);
    out->chip_temp = (float)(int16_t)be16(&r[R_CHIP_TEMP]) / 100.0f;
    if (r[1] & ES_NO_MEASUREMENT) return ESP_ERR_TIMEOUT;
    /* fatal, algorithm, self-diagnostics or memory error: no trusted value */
    if (r[1] & (0x01 | 0x04 | 0x10 | 0x40)) {
        ESP_LOGW(TAG, "error status 0x%02x%02x", r[0], r[1]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (cal_target) {
        ESP_LOGW(TAG, "target calibration to %u ppm: %s", cal_target,
                 (cal & (1u << 4)) ? "done" : "NOT confirmed");
        if (!(cal & (1u << 4))) return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "CO2 %.0f ppm (%.0f hPa)", (double)out->co2, (double)pressure_hpa);
    return ESP_OK;
}

esp_err_t ac_sunrise_measure(ac_sunrise_state_t *st, float pressure_hpa,
                             ac_sunrise_values_t *out)
{
    return run(st, pressure_hpa, 0, out);
}

esp_err_t ac_sunrise_calibrate(ac_sunrise_state_t *st, float pressure_hpa, uint16_t ppm)
{
    ac_sunrise_values_t v;
    return run(st, pressure_hpa, ppm, &v);
}
