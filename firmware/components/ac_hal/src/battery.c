/* Power measurements (v1.3: six AA cells, nothing charged in the device).
 *
 *   pack       AA pack -> 1 MOhm -> GPIO3 -> 220 kOhm -> GND, 100 nF at the
 *              pin.  6 x 1.8 V (fresh lithium) = 10.8 V -> 1.95 V at the pin.
 *              The divider draws ~9 uA at 10.8 V, ~5 uA at 6 V.
 *   regulator  the Pololu S9V11E2A's 4.0 V after the LM66200 ideal diode goes
 *              into the FireBeetle's battery input, where DFRobot's own
 *              1M/1M divider brings it to GPIO0.  A sanity check only.
 *   USB        whether a USB host has enumerated the USB Serial/JTAG port.
 *              A charger or power bank does not count, on purpose: only a
 *              computer should switch the device into CONTINUOUS mode.
 */
#include "ac_hal/ac_hal.h"

#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define PACK_RATIO  ((1000.0f + 220.0f) / 220.0f)
#define REG_RATIO   2.0f
#define N_SAMPLES   16

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_ch_pack, s_ch_reg;

static esp_err_t config_channel(int gpio, adc_channel_t *ch)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, ch);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t c = {
        .atten = ADC_ATTEN_DB_12,        /* ~0..3.1 V full scale */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_oneshot_config_channel(s_adc, *ch, &c);
}

esp_err_t ac_battery_init(void)
{
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT_1 };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (err == ESP_OK) err = config_channel(AC_PIN_PACK_ADC, &s_ch_pack);
    if (err == ESP_OK) err = config_channel(AC_PIN_REG_ADC, &s_ch_reg);
    if (err != ESP_OK) return err;

    adc_cali_curve_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cal, &s_cali) != ESP_OK) {
        /* Uncalibrated readings can be off by 100 mV at the pin, which is
         * over half a volt on the pack.  Say so rather than report a guess. */
        ESP_LOGE(TAG, "no ADC calibration in eFuse; battery level unavailable");
        s_cali = NULL;
    }
    return ESP_OK;
}

static esp_err_t read_mv(adc_channel_t ch, float *mv)
{
    int sum = 0, n = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        int raw = 0, v = 0;
        if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) continue;
        if (adc_cali_raw_to_voltage(s_cali, raw, &v) != ESP_OK) continue;
        sum += v;
        n++;
    }
    if (n == 0) return ESP_FAIL;
    *mv = (float)sum / (float)n;
    return ESP_OK;
}

esp_err_t ac_battery_read(float *pack_v, float *reg_v, bool *usb_host)
{
    if (usb_host) *usb_host = usb_serial_jtag_is_connected();
    if (!s_adc || !s_cali) return ESP_ERR_INVALID_STATE;
    float mv = 0;
    esp_err_t err = read_mv(s_ch_pack, &mv);
    if (err != ESP_OK) return err;
    if (pack_v) *pack_v = PACK_RATIO * mv / 1000.0f;
    if (reg_v && read_mv(s_ch_reg, &mv) == ESP_OK) *reg_v = REG_RATIO * mv / 1000.0f;
    return ESP_OK;
}
