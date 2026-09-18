/* Battery voltage and USB presence on the FireBeetle 2 ESP32-C6.
 *
 * DFRobot's DFR1075 schematic (v1.1): the cell (VBAT) goes through R15/R16,
 * 1M/1M, with 100 nF (C18) across the lower leg, to GPIO0.  So the ADC sees
 * VBAT/2, about 1.6..2.1 V, and the 100 nF is what lets a 500k source drive
 * the ADC's sampling capacitor.  The divider costs ~1.9 uA permanently, and
 * that is already inside DFRobot's 36 uA deep-sleep figure.
 *
 * USB presence comes from the carrier: the FireBeetle's VIN pin carries the
 * USB 5 V (and nothing on battery), divided 68k/100k to ~3.0 V on GPIO18.
 *
 * There is no fuel gauge any more.  ac_core/ac_battery.c turns the voltage
 * into a percentage and explains how far to trust it.
 */
#include "ac_hal/ac_hal.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define DIVIDER_RATIO  2.0f     /* R15 = R16 = 1M */
#define N_SAMPLES      16

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_chan;

esp_err_t ac_battery_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << AC_PIN_USB_SENSE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,       /* the divider defines it */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    adc_unit_t unit;
    err = adc_oneshot_io_to_channel(AC_PIN_BAT_ADC, &unit, &s_chan);
    if (err != ESP_OK) return err;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unit };
    err = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (err != ESP_OK) return err;
    /* 12 dB attenuation: full scale ~3.1 V, comfortably above VBAT/2. */
    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, s_chan, &ccfg);
    if (err != ESP_OK) return err;

    adc_cali_curve_fitting_config_t cal = {
        .unit_id = unit,
        .chan = s_chan,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        /* Uncalibrated readings can be off by 100 mV, which is most of the
         * usable range of a LiPo.  Say so rather than report a guess. */
        ESP_LOGE(TAG, "no ADC calibration in eFuse; battery level unavailable");
        s_cali = NULL;
    }
    return ESP_OK;
}

esp_err_t ac_battery_read(float *volts, bool *usb_present)
{
    if (usb_present) *usb_present = gpio_get_level(AC_PIN_USB_SENSE) != 0;
    if (!s_adc || !s_cali) return ESP_ERR_INVALID_STATE;

    int sum_mv = 0, n = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(s_adc, s_chan, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue;
        sum_mv += mv;
        n++;
    }
    if (n == 0) return ESP_FAIL;
    if (volts) *volts = DIVIDER_RATIO * (float)sum_mv / (float)n / 1000.0f;
    return ESP_OK;
}
