/* Matter data model for the AIR CHECK.
 *
 * Endpoint layout, all standards-compliant device types - no proprietary
 * clusters anywhere:
 *
 *   ep 0  Root Node          + Power Source (battery)
 *   ep 1  Air Quality Sensor 0x002C
 *           Air Quality                          0x005B
 *           PM2.5 Concentration Measurement      0x042A
 *           PM10  Concentration Measurement      0x042D
 *           PM1   Concentration Measurement      0x042C
 *           CO2   Concentration Measurement      0x040D
 *           TVOC  Concentration Measurement      0x042E
 *   ep 2  Temperature Sensor 0x0302
 *   ep 3  Humidity Sensor    0x0307
 *
 * What Apple Home does and does not do with this is documented in
 * docs/APPLE_HOME.md, including the two known gaps (PM1 has no HomeKit
 * equivalent, and the VOC Index is not a concentration).
 */
#pragma once

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
esp_err_t ac_matter_publish_battery(float percent, float volts, bool charging,
                                    bool low);
bool ac_matter_is_commissioned(void);
bool ac_matter_thread_attached(void);
esp_err_t ac_matter_open_commissioning_window(void);
esp_err_t ac_matter_factory_reset(void);
esp_err_t ac_matter_get_pairing_code(char *manual, size_t manual_len,
                                     char *qr, size_t qr_len);

#ifdef __cplusplus
}
#endif
