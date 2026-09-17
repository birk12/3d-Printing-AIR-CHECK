/* Hardware abstraction for the AIR CHECK carrier board.
 *
 * Every pin number, I2C address and command byte in this layer is traceable to
 * a manufacturer document; the mapping is in electronics/schematic/NETLIST.md
 * and is checked by electronics/schematic/design.py.
 */
#ifndef AC_HAL_H
#define AC_HAL_H

#include "ac_core/ac_engine.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pin map (Adafruit ESP32-C6 Feather + ACC-1 carrier) --------------
 * Only GPIO0..GPIO7 are RTC capable on the ESP32-C6, so everything that has
 * to hold a level through deep sleep or wake the chip lives there.  GPIO4, 5,
 * 8, 9 and 15 are strapping pins and are left alone. */
#define AC_PIN_EN_EPD        1   /* A0  - load switch SW3, e-paper rail      */
#define AC_PIN_EN_SPS30      2   /* A5  - load switch SW1, boost input       */
#define AC_PIN_EN_SENS       3   /* A4  - load switch SW2, I2C sensor rail   */
#define AC_PIN_BUTTON        6   /* A2  - active low, EXT1 deep-sleep wake   */
#define AC_PIN_EPD_BUSY      0   /* D11 - input, 100k pull-down              */
#define AC_PIN_EPD_RST       7   /* D9                                       */
#define AC_PIN_EPD_DC       23   /* MISO, repurposed: the panel is write only */
#define AC_PIN_EPD_CS       14   /* D12                                      */
#define AC_PIN_EPD_SCK      21
#define AC_PIN_EPD_MOSI     22
#define AC_PIN_SPS30_TX     16   /* MCU -> sensor, 330 R in series           */
#define AC_PIN_SPS30_RX     17   /* sensor -> MCU                            */
#define AC_PIN_I2C_SDA      19   /* 5k1 pull-up on the Feather               */
#define AC_PIN_I2C_SCL      18
#define AC_PIN_STEMMA_PWR   20   /* driven low: kills the second LDO + NeoPixel */

#define AC_I2C_ADDR_SGP40   0x59
#define AC_I2C_ADDR_SCD41   0x62
#define AC_I2C_ADDR_MAX17048 0x36

/* ---- power rails ------------------------------------------------------ */
esp_err_t ac_hal_init(void);
esp_err_t ac_rail_sps30(bool on);   /* also waits out the boost soft start  */
esp_err_t ac_rail_sensors(bool on);
esp_err_t ac_rail_epd(bool on);
/* Latch the RTC GPIOs so the rails keep their state through deep sleep. */
void ac_rail_hold(bool hold);

/* ---- SPS30 over UART (SHDLC) ------------------------------------------ */
typedef struct {
    float pm1, pm25, pm4, pm10;
    float n05, n1, n25, n4, n10;
    float typical_size;
} ac_sps30_values_t;

esp_err_t ac_sps30_init(void);
esp_err_t ac_sps30_start_measurement(void);
esp_err_t ac_sps30_stop_measurement(void);
esp_err_t ac_sps30_read(ac_sps30_values_t *out);
esp_err_t ac_sps30_sleep(void);
esp_err_t ac_sps30_wake(void);
esp_err_t ac_sps30_start_fan_cleaning(void);
esp_err_t ac_sps30_serial(char *out, size_t n);
/* One complete duty-cycled measurement: power on, run for window_s, average
 * the last third, power off.  Blocks. */
esp_err_t ac_sps30_measure_window(uint32_t window_s, ac_sps30_values_t *out);

/* ---- SGP40 ------------------------------------------------------------ */
esp_err_t ac_sgp40_init(uint32_t sampling_interval_s);
esp_err_t ac_sgp40_self_test(void);
/* Low-power sequence from Sensirion's own example: one discarded measurement
 * to fire the hotplate, the real one 170 ms later, then heater off. */
esp_err_t ac_sgp40_measure(float temperature_c, float humidity_pct,
                           int32_t *raw_out, int32_t *index_out);
esp_err_t ac_sgp40_serial(uint64_t *out);
/* Detach from the I2C bus.  Must be called before the bus itself is torn
 * down, which ac_rail_sensors(false) does. */
void ac_sgp40_detach(void);

/* ---- SCD41 ------------------------------------------------------------ */
esp_err_t ac_scd41_init(void);
esp_err_t ac_scd41_single_shot(float *co2, float *t, float *rh);
esp_err_t ac_scd41_power_down(void);
esp_err_t ac_scd41_wake_up(void);
esp_err_t ac_scd41_set_temperature_offset(float c);
esp_err_t ac_scd41_forced_recalibration(uint16_t target_ppm, int16_t *correction);
esp_err_t ac_scd41_serial(uint64_t *out);
void ac_scd41_detach(void);

/* ---- battery ---------------------------------------------------------- */
esp_err_t ac_battery_read(float *volts, float *percent, bool *charging);
esp_err_t ac_battery_hibernate(bool on);
void ac_battery_detach(void);

/* ---- display ---------------------------------------------------------- */
esp_err_t ac_epd_init(void);
esp_err_t ac_epd_full_update(const uint8_t *fb);
esp_err_t ac_epd_partial_update(const uint8_t *fb);
esp_err_t ac_epd_sleep(void);

/* ---- button ----------------------------------------------------------- */
typedef enum {
    AC_BTN_NONE = 0,
    AC_BTN_SHORT,       /*  < 1.0 s  wake / next screen  */
    AC_BTN_LONG,        /*  3 .. 8 s setup / commissioning */
    AC_BTN_VERY_LONG,   /* 12 .. 20 s factory reset       */
} ac_button_event_t;

#define AC_BTN_LONG_MS        3000
#define AC_BTN_VERY_LONG_MS  12000
#define AC_BTN_ABORT_MS      20000   /* held longer than this = ignored */

esp_err_t ac_button_init(void);
ac_button_event_t ac_button_poll(void);
bool ac_button_pressed(void);

/* ---- persistent storage ----------------------------------------------- */
esp_err_t ac_store_init(void);
esp_err_t ac_store_config_save(const ac_config_t *c);
esp_err_t ac_store_config_load(ac_config_t *c);
esp_err_t ac_store_baseline_save(const ac_baseline_store_t *b);
esp_err_t ac_store_baseline_load(ac_baseline_store_t *b);
esp_err_t ac_store_history_save(const ac_history_t *h);
esp_err_t ac_store_history_load(ac_history_t *h);
esp_err_t ac_store_event_append(const ac_event_record_t *r);
esp_err_t ac_store_event_get(uint32_t index, ac_event_record_t *r);
uint32_t  ac_store_event_count(void);
esp_err_t ac_store_erase_all(void);

#ifdef __cplusplus
}
#endif
#endif
