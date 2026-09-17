#include "ac_core/ac_config.h"
#include <string.h>

/* The profile numbers here are the ones the energy model was run with.
 * tools/battery_calculator/model.py has the same table; firmware/test/host
 * checks that they still agree. */
static const ac_profile_t k_profiles[AC_MODE_COUNT] = {
    /* ECO        */ { .pm_interval_s = 4 * 3600, .pm_window_s = 40,
                       .voc_interval_s = 10, .co2_interval_s = 3600,
                       .icd_slow_poll_s = 15 },
    /* NORMAL     */ { .pm_interval_s = 15 * 60,  .pm_window_s = 60,
                       .voc_interval_s = 10, .co2_interval_s = 15 * 60,
                       .icd_slow_poll_s = 15 },
    /* ACTIVE     */ { .pm_interval_s = 2 * 60,   .pm_window_s = 60,
                       .voc_interval_s = 10, .co2_interval_s = 5 * 60,
                       .icd_slow_poll_s = 5 },
    /* POST_PRINT */ { .pm_interval_s = 5 * 60,   .pm_window_s = 60,
                       .voc_interval_s = 10, .co2_interval_s = 10 * 60,
                       .icd_slow_poll_s = 5 },
    /* CONTINUOUS */ { .pm_interval_s = 0,        .pm_window_s = 60,
                       .voc_interval_s = 1,  .co2_interval_s = 5,
                       .icd_slow_poll_s = 5 },
};

void ac_config_defaults(ac_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = AC_CONFIG_MAGIC;
    c->version = AC_CONFIG_VERSION;
    c->size = (uint16_t)sizeof(*c);
    strncpy(c->name, "AIR CHECK", AC_NAME_MAX - 1);
    strncpy(c->location, "", AC_NAME_MAX - 1);

    c->default_mode = AC_MODE_ECO;
    memcpy(c->profile, k_profiles, sizeof(k_profiles));

    /* PM thresholds.  The 24 h WHO 2021 guideline for PM2.5 is 15 ug/m3 and
     * the interim target 1 is 75; we use those as the ELEVATED and VERY HIGH
     * anchors and put HIGH in between.  These are *reporting* thresholds for
     * a consumer device, not a health verdict - see docs/RISKS.md. */
    c->pm25_elevated   = 15.0f;
    c->pm25_high       = 35.0f;
    c->pm25_very_high  = 75.0f;
    c->pm10_elevated   = 45.0f;
    c->pm10_high       = 100.0f;
    c->pm10_very_high  = 150.0f;

    /* Sensirion VOC Index: 100 is the running 24 h average of this room. */
    c->voc_elevated    = 150;
    c->voc_high        = 250;
    c->voc_very_high   = 350;

    c->co2_elevated    = 1000.0f;
    c->co2_high        = 1500.0f;
    c->co2_very_high   = 2000.0f;

    c->ev_pm25_delta   = 5.0f;
    c->ev_pm25_rate    = 1.5f;
    c->ev_voc_delta    = 40;
    c->ev_confirm_s    = 120;
    c->ev_release_s    = 600;
    c->post_event_s    = 45 * 60;
    c->ev_sensitivity  = 3;

    c->baseline_update_s = 900;
    c->baseline_alpha    = 0.02f;

    c->display_timeout_s     = 20;
    c->display_start_screen  = 0;
    c->display_show_delta    = true;

    c->low_battery_pct      = 20.0f;
    c->critical_battery_pct = 5.0f;

    c->voc_publish_index_as_ppb = true;
    c->co2_self_calibration     = true;
    c->auto_escalate            = true;

    c->crc = ac_config_crc(c);
}

uint32_t ac_config_crc(const ac_config_t *c)
{
    /* CRC-32 over everything except the trailing crc field. */
    const uint8_t *p = (const uint8_t *)c;
    size_t n = sizeof(*c) - sizeof(c->crc);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

bool ac_config_load(ac_config_t *c, const void *blob, size_t len)
{
    ac_config_t tmp;
    if (!blob || len != sizeof(tmp)) return false;
    memcpy(&tmp, blob, sizeof(tmp));
    if (tmp.magic != AC_CONFIG_MAGIC) return false;
    if (tmp.version != AC_CONFIG_VERSION) return false;
    if (tmp.size != (uint16_t)sizeof(tmp)) return false;
    uint32_t want = tmp.crc;
    tmp.crc = 0;
    /* crc was computed with the field zeroed out of the covered range, so
     * recompute over the same bytes */
    if (ac_config_crc(&tmp) != want) return false;
    tmp.crc = want;
    tmp.name[AC_NAME_MAX - 1] = '\0';
    tmp.location[AC_NAME_MAX - 1] = '\0';
    *c = tmp;
    ac_config_validate(c);
    return true;
}

size_t ac_config_save(const ac_config_t *c, void *blob, size_t cap)
{
    if (cap < sizeof(*c)) return 0;
    ac_config_t tmp = *c;
    tmp.magic = AC_CONFIG_MAGIC;
    tmp.version = AC_CONFIG_VERSION;
    tmp.size = (uint16_t)sizeof(tmp);
    tmp.crc = 0;
    tmp.crc = ac_config_crc(&tmp);
    memcpy(blob, &tmp, sizeof(tmp));
    return sizeof(tmp);
}

#define CLAMP(v, lo, hi, n) do { if ((v) < (lo)) { (v) = (lo); (n)++; } \
                                 else if ((v) > (hi)) { (v) = (hi); (n)++; } } while (0)

int ac_config_validate(ac_config_t *c)
{
    int n = 0;

    for (int m = 0; m < AC_MODE_COUNT; m++) {
        ac_profile_t *p = &c->profile[m];
        if (p->pm_interval_s != 0) {
            CLAMP(p->pm_interval_s, 60u, 24u * 3600u, n);
            /* Sensirion: never use the output before 8 s in measurement mode,
             * 30 s for a good compromise.  Refuse anything shorter than 8 s. */
            CLAMP(p->pm_window_s, 8u, 300u, n);
        }
        if (p->voc_interval_s != 0)
            /* SGP40 datasheet: SRAW_VOC sampling interval 0.5 .. 10 s.  The
             * Gas Index Algorithm is validated at 1 s and 10 s. */
            CLAMP(p->voc_interval_s, 1u, 10u, n);
        if (p->co2_interval_s != 0)
            CLAMP(p->co2_interval_s, 5u, 6u * 3600u, n);
        /* Matter 1.4: a SIT ICD must not exceed a 15 s slow poll. */
        CLAMP(p->icd_slow_poll_s, 1u, 15u, n);
    }

    CLAMP(c->pm25_elevated, 1.0f, 500.0f, n);
    CLAMP(c->pm25_high, 1.0f, 800.0f, n);
    CLAMP(c->pm25_very_high, 1.0f, 1000.0f, n);
    if (c->pm25_high <= c->pm25_elevated)      { c->pm25_high = c->pm25_elevated * 2.0f; n++; }
    if (c->pm25_very_high <= c->pm25_high)     { c->pm25_very_high = c->pm25_high * 2.0f; n++; }
    if (c->pm10_high <= c->pm10_elevated)      { c->pm10_high = c->pm10_elevated * 2.0f; n++; }
    if (c->pm10_very_high <= c->pm10_high)     { c->pm10_very_high = c->pm10_high * 2.0f; n++; }

    CLAMP(c->voc_elevated, 1, 500, n);
    CLAMP(c->voc_high, 1, 500, n);
    CLAMP(c->voc_very_high, 1, 500, n);
    if (c->voc_high <= c->voc_elevated)        { c->voc_high = c->voc_elevated + 50; n++; }
    if (c->voc_very_high <= c->voc_high)       { c->voc_very_high = c->voc_high + 50; n++; }

    CLAMP(c->co2_elevated, 400.0f, 5000.0f, n);
    if (c->co2_high <= c->co2_elevated)        { c->co2_high = c->co2_elevated + 400.0f; n++; }
    if (c->co2_very_high <= c->co2_high)       { c->co2_very_high = c->co2_high + 400.0f; n++; }

    CLAMP(c->ev_pm25_delta, 0.5f, 200.0f, n);
    CLAMP(c->ev_pm25_rate, 0.05f, 100.0f, n);
    CLAMP(c->ev_voc_delta, 5, 400, n);
    CLAMP(c->ev_confirm_s, 10u, 3600u, n);
    CLAMP(c->ev_release_s, 30u, 7200u, n);
    CLAMP(c->post_event_s, 60u, 6u * 3600u, n);
    CLAMP(c->ev_sensitivity, 1, 5, n);

    CLAMP(c->baseline_update_s, 60u, 6u * 3600u, n);
    CLAMP(c->baseline_alpha, 0.001f, 0.5f, n);

    CLAMP(c->display_timeout_s, 5u, 600u, n);
    CLAMP(c->display_start_screen, 0, 5, n);

    CLAMP(c->low_battery_pct, 5.0f, 50.0f, n);
    CLAMP(c->critical_battery_pct, 1.0f, 20.0f, n);
    if (c->critical_battery_pct >= c->low_battery_pct) {
        c->critical_battery_pct = c->low_battery_pct / 4.0f;
        n++;
    }
    if (c->default_mode >= AC_MODE_COUNT) { c->default_mode = AC_MODE_ECO; n++; }

    c->name[AC_NAME_MAX - 1] = '\0';
    c->location[AC_NAME_MAX - 1] = '\0';
    if (c->name[0] == '\0') { strncpy(c->name, "AIR CHECK", AC_NAME_MAX - 1); n++; }

    return n;
}

void ac_config_apply_sensitivity(ac_config_t *c, uint8_t s)
{
    if (s < 1) s = 1;
    if (s > 5) s = 5;
    c->ev_sensitivity = s;
    /* 1 = only obvious events, 5 = hair trigger.  The multipliers are chosen
     * so that 3 reproduces the defaults exactly. */
    static const float pm_mult[6]  = { 0, 3.0f, 1.8f, 1.0f, 0.65f, 0.4f };
    static const float rate_mult[6]= { 0, 3.0f, 1.8f, 1.0f, 0.65f, 0.4f };
    static const float voc_mult[6] = { 0, 2.5f, 1.6f, 1.0f, 0.7f,  0.5f };
    static const float conf_mult[6]= { 0, 2.0f, 1.5f, 1.0f, 0.75f, 0.5f };

    ac_config_t d;
    ac_config_defaults(&d);
    c->ev_pm25_delta = d.ev_pm25_delta * pm_mult[s];
    c->ev_pm25_rate  = d.ev_pm25_rate  * rate_mult[s];
    c->ev_voc_delta  = (int32_t)((float)d.ev_voc_delta * voc_mult[s]);
    c->ev_confirm_s  = (uint32_t)((float)d.ev_confirm_s * conf_mult[s]);
    ac_config_validate(c);
}
