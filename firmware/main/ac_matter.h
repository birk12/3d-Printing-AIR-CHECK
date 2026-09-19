/* Matter data model for the AIR CHECK.
 *
 * Endpoint layout, all standards-compliant device types - no proprietary
 * clusters anywhere:
 *
 *   ep 0  Root Node          + Power Source (battery)
 *   ep 1  Air Quality Sensor 0x002C
 *           Air Quality                          0x005B
 *           PM2.5 Concentration Measurement      0x042A  MEA + PEA + AVG (24 h)
 *           PM10  Concentration Measurement      0x042D  MEA + PEA + AVG (24 h)
 *           PM1   Concentration Measurement      0x042C  MEA
 *           CO2   Concentration Measurement      0x040D  MEA + PEA + AVG (24 h)
 *           TVOC  Concentration Measurement      0x042E  MEA + PEA + AVG (24 h)
 *   ep 2  Temperature Sensor 0x0302
 *   ep 3  Humidity Sensor    0x0307
 *
 * What Apple Home does and does not do with this is documented in
 * docs/APPLE_HOME.md, including the two known gaps (PM1 has no HomeKit
 * equivalent, and the VOC Index is not a concentration).  What the dashboard
 * reads, and how, is in docs/DASHBOARD_INTERFACE.md.
 */
#pragma once

#include "pwr_std.h"
#include "ac_core/ac_engine.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ac_matter_init(ac_engine_t *engine);
esp_err_t ac_matter_start(void);
/* Push the engine's current values into the data model.  Cheap when nothing
 * changed: every attribute is compared before it is written, because a write
 * is what triggers a Thread transmission to every subscriber. */
esp_err_t ac_matter_publish(const ac_engine_t *e);
/* Both Power Source clusters: the LFP battery on endpoint 0 and the USB-C
 * input on its own endpoint (docs/MATTER.md).  ext: the module's USB-C is
 * powered; st: pwr_std's view; vbat: cell volts. */
esp_err_t ac_matter_publish_power(const pwr_state_t *st, bool ext, float vbat);
bool ac_matter_is_commissioned(void);
/* Called with true/false when a controller starts/stops Identify. */
void ac_matter_set_identify_cb(void (*cb)(bool on));
/* Publish the configured device name as Basic Information / NodeLabel. */
esp_err_t ac_matter_set_label(const char *label);
bool ac_matter_thread_attached(void);
esp_err_t ac_matter_open_commissioning_window(void);
esp_err_t ac_matter_factory_reset(void);
esp_err_t ac_matter_get_pairing_code(char *manual, size_t manual_len,
                                     char *qr, size_t qr_len);

#ifdef __cplusplus
}
#endif
