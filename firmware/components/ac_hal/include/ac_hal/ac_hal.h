/* Hardware abstraction for the AIR CHECK carrier board.
 *
 * Every pin number, I2C address and command byte in this layer is traceable to
 * a manufacturer document; the mapping is in electronics/schematic/NETLIST.md
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

/* ---- pin map (DFRobot FireBeetle 2 ESP32-C6 + ACC-1 carrier, v1.2) ----
 * From DFRobot's DFR1075 schematic (v1.1) and dimension drawing.  Only
 * GPIO0..GPIO7 are LP (RTC) pads on the ESP32-C6.  GPIO4, 5, 8, 9 and 15 are
 * strapping pins; 9 is also the BOOT button and 15 the board's green LED
 * (to GND through 2k), so both are left alone.  GPIO0 is not on a header: the
 * board wires it to its own 1M/1M battery divider. */
#define AC_PIN_BAT_ADC       0   /* on-board VBAT/2 divider, ADC1 channel 0  */
#define AC_PIN_BUTTON        1   /* active low, LP pad                      */
#define AC_PIN_EN_SEN6X      2   /* load switch SW1, SEN63C supply          */
#define AC_PIN_EN_SENS       3   /* load switch SW2, SGP40 breakout supply  */
#define AC_PIN_SENS_SDA      6   /* LP_I2C SDA, fixed pad on the C6         */
#define AC_PIN_SENS_SCL      7   /* LP_I2C SCL, fixed pad on the C6         */
#define AC_PIN_BOARD_LED    15   /* FireBeetle D13, green, kept off         */
#define AC_PIN_USB_SENSE    18   /* VIN through 68k/100k: high on USB       */
#define AC_PIN_SEN6X_SDA    19   /* header "SDA"                            */
#define AC_PIN_SEN6X_SCL    20   /* header "SCL"                            */
#define AC_PIN_LED_R        21   /* LED cathodes, active low                */
#define AC_PIN_LED_G        22
#define AC_PIN_LED_B        23

#define AC_I2C_ADDR_SGP40   0x59
#define AC_I2C_ADDR_SEN6X   0x6B   /* SEN6x datasheet 5.3 (SEN60 would be 0x6C) */

/* ---- power rails ------------------------------------------------------
 * Both sensors sit behind their own load switch and are unpowered most of the
 * time: the SEN63C draws 3.3 mA even idle, and the SGP40 breakout carries an
 * LDO and a power LED that together draw ~185 uA (EDR-14).  Each rail has its
 * own I2C bus with its pull-ups on the switched side, so an unpowered sensor
 * is never back-fed through a pull-up. */
esp_err_t ac_hal_init(void);
esp_err_t ac_rail_sen6x(bool on);    /* also creates / removes its I2C bus */
esp_err_t ac_rail_sensors(bool on);  /* SGP40 rail and the LP_I2C bus */
/* Latch the rail enables so they keep their state through deep sleep. */
void ac_rail_hold(bool hold);

/* ---- SEN63C: PM1/2.5/4/10, CO2, temperature, humidity -----------------
 * Command IDs, timings and scaling from the SEN6x datasheet v0.5 and
 * Sensirion's embedded-i2c-sen63c driver. */
typedef struct {
    float pm1, pm25, pm4, pm10;     /* ug/m3 */
    float n05, n10;                 /* #/cm3, number concentration */
    float co2;                      /* ppm, < 0 if the window was too short */
    float temperature, humidity;    /* degC, %RH; < -100 / < 0 if unknown */
    uint32_t status;                /* device status register */
} ac_sen6x_values_t;

/* One measurement window.  Powers the module if it is not already running,
 * measures for window_s, averages PM over the settled part, takes the last
 * valid CO2 and T/RH, then stops and powers down - unless keep_running, in
 * which case the next call continues without a restart.  Blocks. */
esp_err_t ac_sen6x_measure_window(uint32_t window_s, bool keep_running,
                                  ac_sen6x_values_t *out);
/* Stop and power down if a keep_running window left the module on. */
void      ac_sen6x_power_off(void);
esp_err_t ac_sen6x_serial(char *out, size_t n);
/* The module's own CO2 automatic self calibration.  Persistent inside the
 * sensor; written only when it differs. */
esp_err_t ac_sen6x_set_asc(bool on);
/* Forced recalibration against a known reference, e.g. 425 ppm outdoors.
 * Call it right after a >= 3 min keep_running window in that air; it stops
 * the measurement, recalibrates and powers the module off. */
esp_err_t ac_sen6x_forced_recalibration(uint16_t target_ppm, int16_t *correction);

/* ---- SGP40 ------------------------------------------------------------ */
esp_err_t ac_sgp40_init(uint32_t sampling_interval_s);
esp_err_t ac_sgp40_self_test(void);
/* Low-power sequence from Sensirion's own example: one discarded measurement
 * to fire the hotplate, the real one 170 ms later, then heater off.  With
 * pulse_rail the breakout is powered for just that sequence (~0.25 s); the
 * SGP40 comes out of power-up in the same idle state the heater-off command
 * leaves it in, so the sequence is the same either way.  The VOC Index
 * algorithm state lives in the MCU and is untouched by the power cycle. */
esp_err_t ac_sgp40_measure(float temperature_c, float humidity_pct,
                           bool pulse_rail, int32_t *raw_out, int32_t *index_out);
esp_err_t ac_sgp40_serial(uint64_t *out);
/* Detach from the I2C bus.  Must be called before the bus itself is torn
 * down, which ac_rail_sensors(false) does. */
void ac_sgp40_detach(void);

/* ---- battery ----------------------------------------------------------
 * Cell voltage through the FireBeetle's own 1M/1M divider (GPIO0), and USB
 * presence through the carrier's VIN divider (GPIO18).  No fuel gauge: the
 * percentage is derived from the voltage in ac_core/ac_battery.h. */
esp_err_t ac_battery_init(void);
esp_err_t ac_battery_read(float *volts, bool *usb_present);

/* ---- status LED -------------------------------------------------------
 * Common anode on VBAT, cathodes sunk by GPIO21/22/23.  "Off" releases the
 * pins to high impedance rather than driving them high, so nothing flows in
 * sleep either way.  Colour bits: 1 = red, 2 = green, 4 = blue. */
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
esp_err_t ac_store_erase_all(void);

#ifdef __cplusplus
}
#endif
#endif
