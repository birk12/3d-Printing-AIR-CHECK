#include "ac_core/ac_types.h"
#include <stdio.h>
#include <string.h>

const char *ac_air_quality_name(ac_air_quality_t q)
{
    switch (q) {
    case AC_AQ_GOOD:      return "GOOD";
    case AC_AQ_ELEVATED:  return "ELEVATED";
    case AC_AQ_HIGH:      return "HIGH";
    case AC_AQ_VERY_HIGH: return "VERY HIGH";
    default:              return "UNKNOWN";
    }
}

const char *ac_event_state_name(ac_event_state_t s)
{
    switch (s) {
    case AC_EV_IDLE:           return "IDLE";
    case AC_EV_POSSIBLE_PRINT: return "POSSIBLE_PRINT";
    case AC_EV_ACTIVE:         return "ACTIVE";
    case AC_EV_POST_PRINT:     return "POST_PRINT";
    case AC_EV_NORMALIZED:     return "NORMALIZED";
    default:                   return "?";
    }
}

const char *ac_mode_name(ac_mode_t m)
{
    switch (m) {
    case AC_MODE_ECO:        return "ECO";
    case AC_MODE_NORMAL:     return "NORMAL";
    case AC_MODE_ACTIVE:     return "ACTIVE";
    case AC_MODE_POST_PRINT: return "POST-PRINT";
    case AC_MODE_CONTINUOUS: return "CONTINUOUS";
    default:                 return "?";
    }
}

const char *ac_device_state_name(ac_device_state_t s)
{
    switch (s) {
    case AC_STATE_BOOT:             return "BOOT";
    case AC_STATE_WARMUP:           return "WARMUP";
    case AC_STATE_NORMAL:           return "NORMAL";
    case AC_STATE_ACTIVE:           return "ACTIVE";
    case AC_STATE_POST_PRINT:       return "POST_PRINT";
    case AC_STATE_SLEEP:            return "SLEEP";
    case AC_STATE_CHARGING:         return "CHARGING";
    case AC_STATE_LOW_BATTERY:      return "LOW_BATTERY";
    case AC_STATE_CRITICAL_BATTERY: return "CRITICAL_BATTERY";
    case AC_STATE_ERROR:            return "ERROR";
    case AC_STATE_COMMISSIONING:    return "COMMISSIONING";
    case AC_STATE_FACTORY_RESET:    return "FACTORY_RESET";
    default:                        return "?";
    }
}

void ac_reason_text(uint32_t mask, char *out, size_t n)
{
    static const struct { uint32_t bit; const char *txt; } tab[] = {
        { AC_REASON_PM25,       "PM2.5"       },
        { AC_REASON_PM10,       "PM10"        },
        { AC_REASON_VOC,        "VOC"         },
        { AC_REASON_CO2,        "CO2"         },
        { AC_REASON_PM_RISING,  "PM rising"   },
        { AC_REASON_VOC_RISING, "VOC rising"  },
    };
    if (!out || n == 0) return;
    out[0] = '\0';
    size_t used = 0;
    int first = 1;
    for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (!(mask & tab[i].bit)) continue;
        const char *sep = first ? "" : " + ";
        size_t need = strlen(sep) + strlen(tab[i].txt);
        if (used + need + 1 >= n) break;
        strcat(out, sep);
        strcat(out, tab[i].txt);
        used += need;
        first = 0;
    }
    if (first) {
        /* nothing set */
        if (n > 1) { strncpy(out, "-", n - 1); out[n - 1] = '\0'; }
    }
}
