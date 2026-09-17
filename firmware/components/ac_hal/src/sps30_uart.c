/* Sensirion SPS30 over its UART (SHDLC) interface.
 *
 * UART rather than I2C on purpose, for two reasons that both come out of the
 * datasheet:
 *   - "For connection cables longer than 20 cm we recommend using the UART
 *     interface, due to its intrinsic robustness against electromagnetic
 *     interference."  The sensor sits on a cable in this design.
 *   - the SPS30's 5 V rail is switched off between measurements.  With I2C the
 *     board's pull-ups would keep feeding its pins while it is unpowered; with
 *     UART both lines go high impedance in deep sleep and nothing flows.
 *
 * Frame format, protocol constants and command codes: SPS30 datasheet v2.0,
 * section 5.
 */
#include "ac_hal/ac_hal.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "sps30";

#define SPS_UART        UART_NUM_1
#define SPS_BAUD        115200
#define SHDLC_START     0x7E
#define SHDLC_STOP      0x7E
#define SHDLC_ADDR      0x00

#define CMD_START_MEAS      0x00
#define CMD_STOP_MEAS       0x01
#define CMD_READ_MEAS       0x03
#define CMD_SLEEP           0x10
#define CMD_WAKE            0x11
#define CMD_START_CLEANING  0x56
#define CMD_DEVICE_INFO     0xD0
#define CMD_RESET           0xD3

static uint8_t checksum(const uint8_t *d, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += d[i];
    return (uint8_t)(~(sum & 0xFF));
}

/* 0x7E and 0x7D are reserved, so the payload is byte stuffed. */
static size_t stuff(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = in[i];
        uint8_t esc = 0;
        switch (b) {
        case 0x7E: esc = 0x5E; break;
        case 0x7D: esc = 0x5D; break;
        case 0x11: esc = 0x31; break;
        case 0x13: esc = 0x33; break;
        default: break;
        }
        if (esc) {
            if (w + 2 > cap) return 0;
            out[w++] = 0x7D;
            out[w++] = esc;
        } else {
            if (w + 1 > cap) return 0;
            out[w++] = b;
        }
    }
    return w;
}

static size_t unstuff(const uint8_t *in, size_t n, uint8_t *out, size_t cap)
{
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == 0x7D && i + 1 < n) {
            uint8_t b;
            switch (in[++i]) {
            case 0x5E: b = 0x7E; break;
            case 0x5D: b = 0x7D; break;
            case 0x31: b = 0x11; break;
            case 0x33: b = 0x13; break;
            default: return 0;
            }
            if (w >= cap) return 0;
            out[w++] = b;
        } else {
            if (w >= cap) return 0;
            out[w++] = in[i];
        }
    }
    return w;
}

static esp_err_t shdlc_xfer(uint8_t cmd, const uint8_t *tx, size_t txlen,
                            uint8_t *rx, size_t rxcap, size_t *rxlen,
                            uint32_t timeout_ms)
{
    uint8_t raw[64];
    size_t n = 0;
    raw[n++] = SHDLC_ADDR;
    raw[n++] = cmd;
    raw[n++] = (uint8_t)txlen;
    if (txlen) { memcpy(&raw[n], tx, txlen); n += txlen; }
    raw[n] = checksum(raw, n);
    n++;

    uint8_t frame[140];
    size_t f = 0;
    frame[f++] = SHDLC_START;
    size_t s = stuff(raw, n, &frame[f], sizeof(frame) - f - 1);
    if (!s) return ESP_ERR_INVALID_SIZE;
    f += s;
    frame[f++] = SHDLC_STOP;

    uart_flush_input(SPS_UART);
    int written = uart_write_bytes(SPS_UART, frame, f);
    if (written != (int)f) return ESP_FAIL;

    /* read until the closing 0x7E */
    uint8_t in[300];
    size_t got = 0;
    bool started = false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline && got < sizeof(in)) {
        uint8_t b;
        int r = uart_read_bytes(SPS_UART, &b, 1, pdMS_TO_TICKS(20));
        if (r != 1) continue;
        if (!started) {
            if (b == SHDLC_START) { started = true; }
            continue;
        }
        if (b == SHDLC_STOP) break;
        in[got++] = b;
    }
    if (!started || got < 4) return ESP_ERR_TIMEOUT;

    uint8_t data[200];
    size_t dn = unstuff(in, got, data, sizeof(data));
    if (dn < 5) return ESP_ERR_INVALID_RESPONSE;
    if (checksum(data, dn - 1) != data[dn - 1]) return ESP_ERR_INVALID_CRC;
    if (data[1] != cmd) return ESP_ERR_INVALID_RESPONSE;
    if (data[2] != 0) {
        ESP_LOGW(TAG, "device state 0x%02x on cmd 0x%02x", data[2], cmd);
        return ESP_ERR_INVALID_STATE;
    }
    size_t payload = data[3];
    if (payload > rxcap) return ESP_ERR_INVALID_SIZE;
    if (rx && payload) memcpy(rx, &data[4], payload);
    if (rxlen) *rxlen = payload;
    return ESP_OK;
}

esp_err_t ac_sps30_init(void)
{
    uart_config_t cfg = {
        .baud_rate = SPS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(SPS_UART, 512, 512, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    ESP_ERROR_CHECK(uart_param_config(SPS_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SPS_UART, AC_PIN_SPS30_TX, AC_PIN_SPS30_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    return ESP_OK;
}

esp_err_t ac_sps30_start_measurement(void)
{
    /* 0x01 = measurement mode, 0x03 = big-endian IEEE754 float output */
    const uint8_t arg[2] = { 0x01, 0x03 };
    return shdlc_xfer(CMD_START_MEAS, arg, sizeof(arg), NULL, 0, NULL, 200);
}

esp_err_t ac_sps30_stop_measurement(void)
{
    return shdlc_xfer(CMD_STOP_MEAS, NULL, 0, NULL, 0, NULL, 200);
}

static float be_float(const uint8_t *p)
{
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                 ((uint32_t)p[2] << 8) | p[3];
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

esp_err_t ac_sps30_read(ac_sps30_values_t *out)
{
    uint8_t rx[40];
    size_t n = 0;
    esp_err_t err = shdlc_xfer(CMD_READ_MEAS, NULL, 0, rx, sizeof(rx), &n, 200);
    if (err != ESP_OK) return err;
    if (n == 0) return ESP_ERR_NOT_FINISHED;     /* no new value yet */
    if (n != 40) return ESP_ERR_INVALID_SIZE;
    out->pm1  = be_float(&rx[0]);
    out->pm25 = be_float(&rx[4]);
    out->pm4  = be_float(&rx[8]);
    out->pm10 = be_float(&rx[12]);
    out->n05  = be_float(&rx[16]);
    out->n1   = be_float(&rx[20]);
    out->n25  = be_float(&rx[24]);
    out->n4   = be_float(&rx[28]);
    out->n10  = be_float(&rx[32]);
    out->typical_size = be_float(&rx[36]);
    return ESP_OK;
}

esp_err_t ac_sps30_sleep(void)
{
    return shdlc_xfer(CMD_SLEEP, NULL, 0, NULL, 0, NULL, 200);
}

esp_err_t ac_sps30_wake(void)
{
    /* The wake-up sequence needs a low pulse on RX first; sending a single
     * 0xFF byte does that, and the module ignores it. */
    const uint8_t pulse = 0xFF;
    uart_write_bytes(SPS_UART, &pulse, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return shdlc_xfer(CMD_WAKE, NULL, 0, NULL, 0, NULL, 200);
}

esp_err_t ac_sps30_start_fan_cleaning(void)
{
    return shdlc_xfer(CMD_START_CLEANING, NULL, 0, NULL, 0, NULL, 200);
}

esp_err_t ac_sps30_serial(char *out, size_t n)
{
    const uint8_t arg = 0x03;            /* serial number */
    uint8_t rx[33] = { 0 };
    size_t len = 0;
    esp_err_t err = shdlc_xfer(CMD_DEVICE_INFO, &arg, 1, rx, sizeof(rx) - 1,
                               &len, 200);
    if (err != ESP_OK) return err;
    rx[len < sizeof(rx) ? len : sizeof(rx) - 1] = '\0';
    strncpy(out, (const char *)rx, n - 1);
    out[n - 1] = '\0';
    return ESP_OK;
}

esp_err_t ac_sps30_measure_window(uint32_t window_s, ac_sps30_values_t *out)
{
    /* Sensirion's low-power application note, section 2: run for at least 30 s
     * before using the output, then average.  We spend two thirds of the
     * window settling and average over the last third, with a floor of 8 s
     * total because the note says never to go below that. */
    if (window_s < 8) window_s = 8;
    uint32_t settle_s = (window_s * 2) / 3;
    if (settle_s > 30) settle_s = 30;
    if (settle_s < 4) settle_s = 4;
    uint32_t avg_s = window_s - settle_s;
    if (avg_s < 2) avg_s = 2;

    esp_err_t err = ac_rail_sps30(true);
    if (err != ESP_OK) return err;
    err = ac_sps30_init();
    if (err == ESP_OK) err = ac_sps30_wake();
    /* a fresh power-up comes straight out of reset in idle mode, so a failed
     * wake is not fatal */
    err = ac_sps30_start_measurement();
    if (err != ESP_OK) { ac_rail_sps30(false); return err; }

    vTaskDelay(pdMS_TO_TICKS(settle_s * 1000));

    ac_sps30_values_t acc;
    memset(&acc, 0, sizeof(acc));
    uint32_t n = 0;
    for (uint32_t i = 0; i < avg_s; i++) {
        ac_sps30_values_t v;
        if (ac_sps30_read(&v) == ESP_OK) {
            acc.pm1 += v.pm1; acc.pm25 += v.pm25;
            acc.pm4 += v.pm4; acc.pm10 += v.pm10;
            acc.n05 += v.n05; acc.n1 += v.n1; acc.n25 += v.n25;
            acc.n4 += v.n4; acc.n10 += v.n10;
            acc.typical_size += v.typical_size;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ac_sps30_stop_measurement();
    ac_rail_sps30(false);

    if (n == 0) return ESP_ERR_TIMEOUT;
    out->pm1 = acc.pm1 / (float)n;   out->pm25 = acc.pm25 / (float)n;
    out->pm4 = acc.pm4 / (float)n;   out->pm10 = acc.pm10 / (float)n;
    out->n05 = acc.n05 / (float)n;   out->n1 = acc.n1 / (float)n;
    out->n25 = acc.n25 / (float)n;   out->n4 = acc.n4 / (float)n;
    out->n10 = acc.n10 / (float)n;
    out->typical_size = acc.typical_size / (float)n;
    ESP_LOGI(TAG, "window %lus -> PM2.5 %.1f ug/m3 from %lu samples",
             (unsigned long)window_s, (double)out->pm25, (unsigned long)n);
    return ESP_OK;
}
