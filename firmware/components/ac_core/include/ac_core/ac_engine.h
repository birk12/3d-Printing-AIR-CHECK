/* The measurement engine: everything that decides *what* happens and *when*,
 * with no reference to any hardware.
 *
 * ac_hal calls ac_engine_tick() with the current time and whatever samples it
 * managed to collect; the engine answers with a plan (which sensor to run
 * next, how long the MCU may sleep, what the status LED should show, what should
 * be published over Matter).  That split is what makes the interesting half of
 * this firmware testable on a workstation.
 */
#ifndef AC_ENGINE_H
#define AC_ENGINE_H

#include "ac_airquality.h"
#include "ac_baseline.h"
#include "ac_config.h"
#include "ac_event.h"
#include "ac_filter.h"
#include "ac_history.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sensirion: "it is recommended to operate the sensor for a minimum of 30 s
 * before using the measurement outputs" and never below 8 s.  The engine
 * enforces both ends. */
#define AC_SPS30_MIN_WINDOW_S   8u
#define AC_SPS30_REC_WINDOW_S  30u
/* SGP40: "time until reliably detecting VOC events < 60 s", "time until the
 * specifications are met < 1 h".  We publish after the first, and flag the
 * reading as provisional until the second. */
#define AC_SGP40_USABLE_S      60u
#define AC_SGP40_SPEC_S      3600u
/* SCD41 single shot: the first reading after a power cycle is discarded. */
#define AC_SCD41_DISCARD_FIRST  1

typedef enum {
    AC_ACT_NONE = 0,
    AC_ACT_SAMPLE_PM,
    AC_ACT_SAMPLE_VOC,
    AC_ACT_SAMPLE_CO2,
    AC_ACT_FAN_CLEAN,
    AC_ACT_SAVE_STATE,
} ac_action_t;

typedef struct {
    ac_action_t action;
    uint32_t    pm_window_s;     /* only meaningful for AC_ACT_SAMPLE_PM */
    uint32_t    sleep_ms;        /* how long the caller may sleep */
    bool        status_dirty;
    bool        publish_dirty;   /* a reported Matter attribute changed */
} ac_plan_t;

typedef struct {
    bool sps30_ok, sgp40_ok, scd41_ok, battery_ok;
    uint16_t sps30_errors, sgp40_errors, scd41_errors;
    char last_error[48];
} ac_health_t;

typedef struct {
    ac_config_t   cfg;
    ac_baseline_t base;
    ac_event_det_t ev;
    ac_history_t  hist;
    ac_health_t   health;

    ac_sample_t   last;          /* most recent merged sample */
    ac_aq_result_t aq;

    ac_ema_t  f_pm25, f_pm10, f_voc, f_co2;
    ac_rate_t r_pm25, r_voc;
    ac_stats_t st_pm25;

    ac_device_state_t state;
    ac_mode_t mode;
    ac_time_ms_t boot_ms;
    ac_time_ms_t state_since;
    ac_time_ms_t next_pm, next_voc, next_co2;
    ac_time_ms_t last_fan_clean;
    ac_time_ms_t sps30_on_since;

    uint32_t voc_samples;
    bool     warm;               /* warm-up complete, values publishable */
    bool     charging;
    bool     usb_present;

    /* last published values, so we only mark publish_dirty on a real change */
    float pub_pm25, pub_pm10, pub_pm1, pub_co2, pub_t, pub_rh, pub_batt;
    int32_t pub_voc;
    uint8_t pub_aq;
} ac_engine_t;

void ac_engine_init(ac_engine_t *e, const ac_config_t *cfg, ac_time_ms_t now);

/* Merge a partial observation.  Any field whose _fresh flag is set replaces
 * the engine's copy; the rest is carried forward. */
void ac_engine_submit(ac_engine_t *e, const ac_sample_t *s);

/* Advance the state machine and return the next thing to do. */
ac_plan_t ac_engine_tick(ac_engine_t *e, ac_time_ms_t now);

void ac_engine_set_mode(ac_engine_t *e, ac_mode_t m, ac_time_ms_t now);
void ac_engine_set_power(ac_engine_t *e, bool usb_present, bool charging,
                         float battery_pct, float battery_v, ac_time_ms_t now);
void ac_engine_set_state(ac_engine_t *e, ac_device_state_t s, ac_time_ms_t now);
void ac_engine_sensor_failed(ac_engine_t *e, ac_action_t which, const char *why);
void ac_engine_baseline_reset(ac_engine_t *e);
void ac_engine_factory_reset(ac_engine_t *e, ac_time_ms_t now);

const ac_profile_t *ac_engine_profile(const ac_engine_t *e);
bool ac_engine_values_valid(const ac_engine_t *e);
uint32_t ac_engine_uptime_s(const ac_engine_t *e, ac_time_ms_t now);

#ifdef __cplusplus
}
#endif
#endif
