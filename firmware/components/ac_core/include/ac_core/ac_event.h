/* Emission event detection, without talking to the printer.
 *
 * The detector is deliberately conservative.  Two independent guards keep it
 * from firing on ordinary room noise:
 *   1. a trigger must hold continuously for cfg->ev_confirm_s before the
 *      device commits to ACTIVE;
 *   2. the release path is separately timed (cfg->ev_release_s), so a reading
 *      that dips below the threshold for one sample does not end the event.
 *
 * The state names are the ones in the brief:
 *   IDLE -> POSSIBLE_PRINT -> ACTIVE -> POST_PRINT -> NORMALIZED -> IDLE
 */
#ifndef AC_EVENT_H
#define AC_EVENT_H

#include "ac_baseline.h"
#include "ac_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t started_unix;      /* 0 when the device has no clock */
    ac_time_ms_t started_ms;
    uint32_t duration_s;
    uint32_t recovery_s;        /* time from peak back to baseline + hysteresis */
    float base_pm25;
    float peak_pm1, peak_pm25, peak_pm10;
    int32_t base_voc, peak_voc;
    float base_co2, peak_co2, co2_delta;
    bool complete;
} ac_event_record_t;

typedef struct {
    ac_event_state_t state;
    ac_time_ms_t state_since;
    ac_time_ms_t trigger_since;   /* 0 = not currently triggered */
    ac_time_ms_t clear_since;
    ac_event_record_t cur;
    ac_time_ms_t peak_ms;
    uint32_t events_total;
} ac_event_det_t;

void ac_event_init(ac_event_det_t *d);

/* Feed one evaluated sample.  Returns true when the state changed.
 * `finished` receives the completed record when an event ends (may be NULL). */
bool ac_event_update(ac_event_det_t *d, const ac_config_t *cfg,
                     const ac_baseline_t *base, const ac_sample_t *s,
                     float pm25_rate, float voc_rate,
                     ac_event_record_t *finished);

/* True while the baseline must not track, i.e. during POSSIBLE_PRINT, ACTIVE
 * and POST_PRINT. */
bool ac_event_should_freeze_baseline(const ac_event_det_t *d);
/* The measurement mode this detector wants right now. */
ac_mode_t ac_event_preferred_mode(const ac_event_det_t *d, const ac_config_t *cfg);

#ifdef __cplusplus
}
#endif
#endif
