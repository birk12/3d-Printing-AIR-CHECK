/* Service console on the USB-C port (USB Serial/JTAG).
 *
 * The line protocol tools/configuration/aircheck_config.py speaks:
 *
 *   config get                 every setting, one "key = value" per line
 *   config set <key> <value>   change one, validate, store, apply
 *   baseline reset             adopt the current air as the room baseline
 *   co2 frc <ppm>              fresh-air CO2 calibration (device outdoors!)
 *   events dump                the stored event log
 *   diag                       state, mode, health, last values, battery
 *
 * It is only started while USB power is present: configuring the device needs
 * the cable anyway, and on battery there is nothing listening.
 */
#pragma once

#include "ac_core/ac_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ac_engine_t *engine;
    void (*lock)(void);
    void (*unlock)(void);
    /* Store the engine's (already validated) configuration and apply side
     * effects such as the Matter NodeLabel and the Sunrise's ABC setting. */
    void (*config_changed)(void);
    /* Ask the measurement task for a fresh-air CO2 calibration. */
    void (*request_frc)(uint16_t ppm);
    bool (*thread_attached)(void);
} ac_console_ctx_t;

/* Idempotent: the first call starts the console, later calls do nothing. */
void ac_console_start(const ac_console_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
