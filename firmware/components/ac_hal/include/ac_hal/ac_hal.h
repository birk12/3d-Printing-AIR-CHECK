/* Hardware abstraction for the AIR CHECK (v1.3: off-the-shelf modules only).
 *
 * Every pin number, I2C address and command byte in this layer is traceable to
 * a manufacturer document; the wiring is in electronics/schematic/NETLIST.md
 * and is checked by electronics/schematic/design.py.
 */
#ifndef AC_HAL_H
#define AC_HAL_H

#include "ac_core/ac_engine.h"
#include "ac_core/ac_status.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- pin map (DFRobot FireBeetle 2 ESP32-C6, modules wired to it) -------
 * Only GPIO0..GPIO7 are LP pads on the ESP32-C6.  GPIO4, 5, 8, 9 and 15 are
 * strapping pins; 9 is also the BOOT button and 15 the board's green LED, so
 * all five are left alone.  GPIO0 is wired on the board to its own 1M/1M
 * divider on the battery input, which since v1.3 carries the regulated 4.0 V,
 * not the cells - the pack is measured separately on GPIO3. */
#define AC_PIN_REG_ADC       0   /* on-board divider: regulator output / 2   */
#define AC_PIN_BUTTON        1   /* panel button to GND, internal pull-up   */
#define AC_PIN_SEN_EN        2   /* Pololu #2810 ON pin: SEN62 supply       */
#define AC_PIN_PACK_ADC      3   /* AA pack through 1M / 220k               */
#define AC_PIN_SENS_SDA      6   /* LP_I2C SDA: SGP40 + SHT40, always on    */
#define AC_PIN_SENS_SCL      7   /* LP_I2C SCL                              */
#define AC_PIN_CO2_EN       14   /* Sunrise EN                              */
#define AC_PIN_BOARD_LED    15   /* FireBeetle green LED, kept off          */
#define AC_PIN_CO2_SDA      17   /* Sunrise SDA (HP I2C, re-routed per use) */
#define AC_PIN_CO2_SCL      21   /* Sunrise SCL                             */
#define AC_PIN_CO2_IO       18   /* Sunrise VDDIO and its 10k pull-ups      */
#define AC_PIN_SEN_SDA      19   /* SEN62 SDA (HP I2C), header "SDA"        */
#define AC_PIN_SEN_SCL      20   /* SEN62 SCL, header "SCL"                 */
/* GPIO16 is U0TXD: the ROM boot log toggles it after every reset.  On the
 * red LED that is a faint flicker; on the Sunrise's bus it would drive a
 * sensor that must see no signal while EN is low (TDE7318). */
#define AC_PIN_LED_R        16   /* LED cathodes, active low                */
#define AC_PIN_LED_G        22
#define AC_PIN_LED_B        23

#define AC_I2C_ADDR_SGP40   0x59
#define AC_I2C_ADDR_SHT4X   0x44   /* SHT40-AD1B on the Seeed Grove board */
#define AC_I2C_ADDR_SEN6X   0x6B   /* SEN6x datasheet 4.3 */
#define AC_I2C_ADDR_SUNRISE 0x68   /* Senseair TDE5531 table 1 */

/* ---- buses and rails ----------------------------------------------------
 * LP_I2C (GPIO6/7) is always up: the SGP40 and SHT40 boards are powered all
 * the time and neither carries an LDO or a lit LED.  The ESP32-C6 has one HP
 * I2C controller; it is routed to the SEN62's pins or the Sunrise's pins for
 * as long as one of them is being talked to, never both. */
esp_err_t ac_hal_init(void);
/* SEN62 power (Pololu switch) and its bus on GPIO19/20. */
esp_err_t ac_rail_sen6x(bool on);
/* Latch the sensor enables so they keep their state through sleep. */
void ac_rail_hold(bool hold);
/* Long waits inside a measurement (the SEN62 window, the Sunrise's 10 s)
 * sleep in 1 s slices and call this hook in between, so the app can keep the
 * VOC channel on its 10 s grid.  The hook must not touch the HP I2C bus. */
typedef void (*ac_wait_hook_t)(void);
void ac_hal_set_wait_hook(ac_wait_hook_t hook);
void ac_hal_wait_ms(uint32_t ms);
/* The HP I2C controller, routed to one pin pair at a time (internal). */
esp_err_t ac_hp_bus_open(int sda, int scl);
void      ac_hp_bus_close(int sda, int scl);

/* ---- SEN62: PM1/2.5/4/10 ------------------------------------------------
 * Command IDs, timings and scaling: SEN6x datasheet v0.92, section 4.8. */
typedef struct {
    float pm1, pm25, pm4, pm10;     /* ug/m3 */
    float n05, n10;                 /* #/cm3, number concentration */
    uint32_t status;                /* device status register */
    uint32_t samples;               /* seconds averaged */
} ac_sen6x_values_t;

/* One window: power up if not running, discard AC_PM_SETTLE_S, average the
 * rest; stop and power down unless keep_running.  Blocks. */
esp_err_t ac_sen6x_measure_window(uint32_t window_s, bool keep_running,
                                  ac_sen6x_values_t *out);
void      ac_sen6x_power_off(void);
esp_err_t ac_sen6x_serial(char *out, size_t n);

/* ---- Senseair Sunrise 006-0-0008: CO2 -----------------------------------
 * Single-measurement mode, powered down (EN low, VDDIO off) in between.  The
 * ABC state lives in the host and goes back into the sensor before every
 * measurement (Senseair TDE5531 3.4).  Pressure compensation from the
 * configured altitude. */
#define AC_SUNRISE_STATE_LEN 24     /* registers 0xC4 .. 0xDB */
typedef struct {
    uint8_t  regs[AC_SUNRISE_STATE_LEN];
    bool     valid;                 /* false until the first measurement */
    uint32_t abc_ms;                /* uptime not yet added to ABC Time */
} ac_sunrise_state_t;

typedef struct {
    float    co2;                   /* ppm, pressure compensated, unfiltered */
    float    chip_temp;             /* degC */
    uint8_t  error_status;
} ac_sunrise_values_t;

/* Checks the EEPROM configuration (single mode, 32 samples, IIR off, ABC per
 * `abc`, nRDY unused, pressure compensation on, ABC target 425 ppm) and
 * rewrites it only if it differs - the EEPROM is good for 10 000 writes. */
esp_err_t ac_sunrise_setup(bool abc);
esp_err_t ac_sunrise_measure(ac_sunrise_state_t *st, float pressure_hpa,
                             ac_sunrise_values_t *out);
/* Fresh-air calibration: target calibration to `ppm` on the next measurement.
 * The device must have been in that air for a few minutes. */
esp_err_t ac_sunrise_calibrate(ac_sunrise_state_t *st, float pressure_hpa,
                               uint16_t ppm);

/* ---- SHT40: temperature and humidity -------------------------------------
 * High-repeatability single shot (0xFD), SHT4x datasheet 4.5. */
esp_err_t ac_sht4x_measure(float *t_c, float *rh);

/* ---- SGP40 ------------------------------------------------------------ */
esp_err_t ac_sgp40_init(uint32_t sampling_interval_s);
esp_err_t ac_sgp40_self_test(void);
/* Sensirion's tested low-power sequence: one discarded measurement to heat
 * the hotplate, the real one 170 ms later, heater off.  T/RH compensation
 * from the SHT40 reading taken just before. */
esp_err_t ac_sgp40_measure(float temperature_c, float humidity_pct,
                           int32_t *raw_out, int32_t *index_out);
esp_err_t ac_sgp40_serial(uint64_t *out);

/* ---- power ---------------------------------------------------------------
 * Pack voltage through the 1M/220k divider on GPIO3, the regulator output
 * through the FireBeetle's own divider on GPIO0, and whether a USB host is
 * attached (USB Serial/JTAG enumerated). */
esp_err_t ac_battery_init(void);
esp_err_t ac_battery_read(float *pack_volts, float *reg_volts, bool *usb_host);

/* ---- status LED -------------------------------------------------------
 * Common anode on the 4.0 V regulator output, cathodes sunk by
 * GPIO21/22/23.  "Off" releases the pins to high impedance rather than
 * driving them high.  Colour bits: 1 = red, 2 = green, 4 = blue. */
esp_err_t ac_led_init(void);
void      ac_led_set(uint8_t rgb_bits);

/* ---- button ----------------------------------------------------------- */
typedef enum {
    AC_BTN_NONE = 0,
    AC_BTN_SHORT,       /*  < 3 s    show the air quality on the LED */
    AC_BTN_LONG,        /*  3 .. 8 s  setup / commissioning */
    AC_BTN_CALIBRATE,   /*  8 .. 12 s fresh-air CO2 calibration */
    AC_BTN_VERY_LONG,   /* 12 .. 20 s factory reset       */
} ac_button_event_t;

/* thresholds: AC_HOLD_* in ac_core/ac_status.h, so the LED can show them */
esp_err_t ac_button_init(void);
ac_button_event_t ac_button_poll(void);
bool ac_button_pressed(void);
uint32_t ac_button_held_ms(void);   /* 0 when not pressed */

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
/* Opaque blobs: the Sunrise ABC state and the pack's energy counter. */
esp_err_t ac_store_blob_save(const char *key, const void *data, size_t len);
esp_err_t ac_store_blob_load(const char *key, void *data, size_t len);
esp_err_t ac_store_erase_all(void);

#ifdef __cplusplus
}
#endif
#endif
