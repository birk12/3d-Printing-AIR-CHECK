/* Power measurements (v1.4: LFP power module C, nothing but the module
 * charges anything).
 *
 *   VBAT_S  cell / 2 from the module's 470k/470k divider, 100 nF at GPIO3,
 *           ADC_ATTEN_DB_6 (0..1.9 V; a full cell is 1.83 V at the pin).
 *   PWR-K   one node carries EXT, CHG_N and FLT_N (Power-Standard README
 *           section 3), 100 nF at GPIO4, ADC_ATTEN_DB_12; decoded in ac_core.
 *   VSYS    the FireBeetle's battery input through its own 1M/1M divider on
 *           GPIO0: 3.90 V from the regulator, always.  Sanity only.
 *   CE      GPIO5: output high = pause charging, input = charging allowed.
 *           The #6091's pull-down holds CE low through a reset: fail-safe.
 */
#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery";

#define VBAT_RATIO  2.0f
#define VSYS_RATIO  2.0f
#define N_SAMPLES   16

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali6, s_cali12;
static adc_channel_t s_ch_vbat, s_ch_ladder, s_ch_vsys;

static esp_err_t config_channel(int gpio, adc_atten_t atten, adc_channel_t *ch)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, ch);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t c = { .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT };
    return adc_oneshot_config_channel(s_adc, *ch, &c);
}

static adc_cali_handle_t make_cali(adc_atten_t atten)
{
    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1, .atten = atten, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_handle_t h = NULL;
    if (adc_cali_create_scheme_curve_fitting(&cal, &h) != ESP_OK) return NULL;
    return h;
}

esp_err_t ac_battery_init(void)
{
    /* Input without any pull (gpio_reset_pin would enable the pull-up for a
     * moment): the #6091's pull-down keeps CE low = charging allowed. */
    gpio_config_t ce = {
        .pin_bit_mask = 1ULL << AC_PIN_CE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ce);

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (err == ESP_OK) err = config_channel(AC_PIN_VBAT_S, ADC_ATTEN_DB_6, &s_ch_vbat);
    if (err == ESP_OK) err = config_channel(AC_PIN_PWR_K, ADC_ATTEN_DB_12, &s_ch_ladder);
    if (err == ESP_OK) err = config_channel(AC_PIN_REG_ADC, ADC_ATTEN_DB_12, &s_ch_vsys);
    if (err != ESP_OK) return err;
    s_cali6 = make_cali(ADC_ATTEN_DB_6);
    s_cali12 = make_cali(ADC_ATTEN_DB_12);
    if (!s_cali6 || !s_cali12) {
        /* Uncalibrated readings can be off by 100 mV at the pin - more than
         * the whole LFP plateau.  Say so rather than report a guess. */
        ESP_LOGE(TAG, "no ADC calibration in eFuse; battery level unavailable");
    }
    return ESP_OK;
}

static esp_err_t read_mv(adc_channel_t ch, adc_cali_handle_t cali, int n, float *mv)
{
    if (!cali) return ESP_ERR_INVALID_STATE;
    int sum = 0, got = 0;
    for (int i = 0; i < n; i++) {
        int raw = 0, v = 0;
        if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(cali, raw, &v) != ESP_OK) continue;
        sum += v;
        got++;
    }
    if (got == 0) return ESP_FAIL;
    *mv = (float)sum / (float)got;
    return ESP_OK;
}

esp_err_t ac_battery_read(float *vbat, float ladder_v[2], float *vsys, bool *usb_host)
{
    if (usb_host) *usb_host = usb_serial_jtag_is_connected();
    if (!s_adc) return ESP_ERR_INVALID_STATE;
    float mv = 0;
    esp_err_t err = read_mv(s_ch_vbat, s_cali6, N_SAMPLES, &mv);
    if (err != ESP_OK) return err;
    if (vbat) *vbat = VBAT_RATIO * mv / 1000.0f;
    for (int i = 0; i < 2; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(50));
        float l = 0;
        if (read_mv(s_ch_ladder, s_cali12, 4, &l) != ESP_OK) return ESP_FAIL;
        if (ladder_v) ladder_v[i] = l / 1000.0f;
    }
    if (vsys && read_mv(s_ch_vsys, s_cali12, N_SAMPLES, &mv) == ESP_OK)
        *vsys = VSYS_RATIO * mv / 1000.0f;
    return ESP_OK;
}

void ac_battery_ce(int mode)
{
    if (mode == 0) {
        gpio_set_direction(AC_PIN_CE, GPIO_MODE_INPUT);
        return;
    }
    gpio_set_level(AC_PIN_CE, 1);
    gpio_set_direction(AC_PIN_CE, GPIO_MODE_OUTPUT);
    if (mode == 2) {
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_direction(AC_PIN_CE, GPIO_MODE_INPUT);
    }
}
