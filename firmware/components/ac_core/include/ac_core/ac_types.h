/* 3D Printing AIR CHECK - shared value types.
 *
 * Everything in ac_core is plain C99 with no platform dependency so that the
 * whole measurement pipeline can be compiled and exercised on a workstation.
 * The ESP-IDF layer lives in ac_hal and only supplies samples and time.
 */
#ifndef AC_TYPES_H
#define AC_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AC_INVALID_F   (-1.0f)     /* a measurement we do not have */

/* Monotonic milliseconds since boot. */
typedef uint64_t ac_time_ms_t;

/* One complete observation.  Fields that were not measured in this cycle keep
 * the previous value and have their _fresh flag cleared, so a consumer can
 * tell "unchanged" from "not measured". */
typedef struct {
    ac_time_ms_t t;

    float pm1;          /* ug/m3 */
    float pm25;         /* ug/m3 */
    float pm4;          /* ug/m3 */
    float pm10;         /* ug/m3 */
    float pn05;         /* #/cm3 */
    float pn10;         /* #/cm3 */
    float typical_size; /* um */
    bool  pm_fresh;

    int32_t voc_index;  /* 1..500, Sensirion VOC Index; <0 = not available */
    int32_t voc_raw;    /* SRAW_VOC ticks */
    bool    voc_fresh;

    float co2;          /* ppm */
    bool  co2_fresh;

    float temperature;  /* degC */
    float humidity;     /* %RH */
    bool  th_fresh;

    float battery_v;
    float battery_pct;
    bool  charging;
} ac_sample_t;

typedef enum {
    AC_AQ_UNKNOWN = 0,
    AC_AQ_GOOD,
    AC_AQ_ELEVATED,
    AC_AQ_HIGH,
    AC_AQ_VERY_HIGH,
} ac_air_quality_t;

/* Why the device is in that air-quality state.  Bitmask so that several
 * reasons can be reported at once ("PM2.5 + VOC elevated"). */
typedef enum {
    AC_REASON_NONE      = 0,
    AC_REASON_PM25      = 1 << 0,
    AC_REASON_PM10      = 1 << 1,
    AC_REASON_VOC       = 1 << 2,
    AC_REASON_CO2       = 1 << 3,
    AC_REASON_PM_RISING = 1 << 4,
    AC_REASON_VOC_RISING= 1 << 5,
} ac_reason_t;

typedef enum {
    AC_EV_IDLE = 0,
    AC_EV_POSSIBLE_PRINT,
    AC_EV_ACTIVE,
    AC_EV_POST_PRINT,
    AC_EV_NORMALIZED,
} ac_event_state_t;

typedef enum {
    AC_MODE_ECO = 0,
    AC_MODE_NORMAL,
    AC_MODE_ACTIVE,
    AC_MODE_POST_PRINT,
    AC_MODE_CONTINUOUS,
    AC_MODE_COUNT,
} ac_mode_t;

typedef enum {
    AC_STATE_BOOT = 0,
    AC_STATE_WARMUP,
    AC_STATE_NORMAL,
    AC_STATE_ACTIVE,
    AC_STATE_POST_PRINT,
    AC_STATE_SLEEP,
    AC_STATE_CHARGING,
    AC_STATE_LOW_BATTERY,
    AC_STATE_CRITICAL_BATTERY,
    AC_STATE_ERROR,
    AC_STATE_COMMISSIONING,
    AC_STATE_FACTORY_RESET,
    AC_STATE_COUNT,
} ac_device_state_t;

const char *ac_air_quality_name(ac_air_quality_t q);
const char *ac_event_state_name(ac_event_state_t s);
const char *ac_mode_name(ac_mode_t m);
const char *ac_device_state_name(ac_device_state_t s);
/* Human readable reason, e.g. "PM2.5 + VOC elevated".  Writes at most n bytes
 * and always NUL terminates. */
void ac_reason_text(uint32_t mask, char *out, size_t n);

#ifdef __cplusplus
}
#endif
#endif /* AC_TYPES_H */
