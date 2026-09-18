/* Runtime configuration.  Nothing in here requires a firmware rebuild to
 * change: the struct is persisted as one NVS blob and every field is
 * reachable over the Matter interface or the serial console. */
#ifndef AC_CONFIG_H
#define AC_CONFIG_H

#include "ac_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_CONFIG_MAGIC    0x41434B31u   /* "ACK1" */
#define AC_CONFIG_VERSION  3   /* v3: SEN63C, one window for PM + CO2 (v1.2) */
#define AC_NAME_MAX        33

/* Shortest SEN63C window worth having: its CO2 output reads "unknown" for the
 * first 22..24 s of a measurement and its PM output needs 30 s (typ) to
 * settle.  Sensirion SEN6x datasheet v0.5 Table 1, embedded-i2c-sen63c. */
#define AC_PM_MIN_WINDOW_S  30u

/* One measurement cadence.  Mirrors tools/battery_calculator/model.py - if you
 * change a number here, re-run the model before believing the runtime.
 * Since v1.2 one SEN63C window delivers PM, CO2, temperature and humidity
 * together, so there is no separate CO2 cadence. */
typedef struct {
    uint32_t pm_interval_s;    /* 0 = continuous */
    uint32_t pm_window_s;      /* time in SEN63C measurement mode */
    uint32_t voc_interval_s;   /* 0 = VOC off */
    uint32_t icd_slow_poll_s;
} ac_profile_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;

    char     name[AC_NAME_MAX];      /* shown in the commissioning label and the logs */
    char     location[AC_NAME_MAX];  /* free text, Apple Home owns the real one */

    ac_mode_t default_mode;
    ac_profile_t profile[AC_MODE_COUNT];

    /* air quality thresholds, ug/m3 / index / ppm */
    float pm25_elevated, pm25_high, pm25_very_high;
    float pm10_elevated, pm10_high, pm10_very_high;
    int32_t voc_elevated, voc_high, voc_very_high;
    float co2_elevated, co2_high, co2_very_high;

    /* event detection */
    float    ev_pm25_delta;        /* ug/m3 above baseline to arm */
    float    ev_pm25_rate;         /* ug/m3 per minute to arm */
    int32_t  ev_voc_delta;         /* VOC index points above baseline to arm */
    uint32_t ev_confirm_s;         /* how long the trigger must hold */
    uint32_t ev_release_s;         /* how long it must be clear again */
    uint32_t post_event_s;         /* POST_PRINT duration */
    uint8_t  ev_sensitivity;       /* 1 = least sensitive .. 5 = most */

    /* baseline */
    uint32_t baseline_update_s;
    float    baseline_alpha;       /* EMA weight per update, 0..1 */

    /* status LED and battery */
    bool     led_show_air_quality;  /* a button press flashes the air quality colour */
    uint32_t battery_interval_s;    /* how often the battery voltage is read */

    /* battery */
    float low_battery_pct;
    float critical_battery_pct;

    /* behaviour */
    bool voc_publish_index_as_ppb; /* see docs/MATTER.md - honest default is on,
                                    * with the caveat documented everywhere */
    bool co2_self_calibration;     /* the SEN63C's own ASC, docs/CALIBRATION.md */
    bool auto_escalate;            /* switch to ACTIVE on a detected event */

    uint32_t crc;
} ac_config_t;

void ac_config_defaults(ac_config_t *c);
/* Returns false and leaves *c untouched if the blob is not usable. */
bool ac_config_load(ac_config_t *c, const void *blob, size_t len);
size_t ac_config_save(const ac_config_t *c, void *blob, size_t cap);
/* Clamps every field into a sane range.  Returns the number of fields it had
 * to correct, so callers can log a warning instead of silently accepting
 * nonsense. */
int ac_config_validate(ac_config_t *c);
uint32_t ac_config_crc(const ac_config_t *c);
/* Applies the 1..5 sensitivity dial to the event thresholds. */
void ac_config_apply_sensitivity(ac_config_t *c, uint8_t sensitivity);

#ifdef __cplusplus
}
#endif
#endif
