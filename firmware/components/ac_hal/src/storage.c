/* Persistent state in NVS.
 *
 * Four things survive a reboot: the configuration, the air-quality baseline,
 * the aggregated history and the event log.  Each is written as one blob with
 * its own CRC so a half-finished write is detected and discarded rather than
 * half-applied.
 *
 * Write budget matters here: NVS lives in the same flash as the firmware and
 * has a finite erase count.  History is therefore only flushed on a state
 * change or every few hours, never on every sample.
 */
#include "ac_hal/ac_hal.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "store";
#define NS          "aircheck"
#define K_CONFIG    "cfg"
#define K_BASELINE  "base"
#define K_HISTORY   "hist"
#define K_EVENTS    "events"
#define K_EVCOUNT   "evn"
#define MAX_EVENTS  32          /* a rolling log, oldest overwritten */

esp_err_t ac_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t blob_save(const char *key, const void *data, size_t n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, data, n);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t blob_load(const char *key, void *data, size_t n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = n;
    err = nvs_get_blob(h, key, data, &len);
    nvs_close(h);
    if (err == ESP_OK && len != n) return ESP_ERR_INVALID_SIZE;
    return err;
}

esp_err_t ac_store_config_save(const ac_config_t *c)
{
    uint8_t blob[sizeof(ac_config_t)];
    size_t n = ac_config_save(c, blob, sizeof(blob));
    if (!n) return ESP_ERR_INVALID_SIZE;
    return blob_save(K_CONFIG, blob, n);
}

esp_err_t ac_store_config_load(ac_config_t *c)
{
    uint8_t blob[sizeof(ac_config_t)];
    esp_err_t err = blob_load(K_CONFIG, blob, sizeof(blob));
    if (err != ESP_OK) return err;
    if (!ac_config_load(c, blob, sizeof(blob))) {
        ESP_LOGW(TAG, "stored config failed its CRC, using defaults");
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t ac_store_baseline_save(const ac_baseline_store_t *b)
{
    return blob_save(K_BASELINE, b, sizeof(*b));
}

esp_err_t ac_store_baseline_load(ac_baseline_store_t *b)
{
    return blob_load(K_BASELINE, b, sizeof(*b));
}

esp_err_t ac_store_history_save(const ac_history_t *h)
{
    return blob_save(K_HISTORY, h, sizeof(*h));
}

esp_err_t ac_store_history_load(ac_history_t *h)
{
    return blob_load(K_HISTORY, h, sizeof(*h));
}

uint32_t ac_store_event_count(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    uint32_t n = 0;
    nvs_get_u32(h, K_EVCOUNT, &n);
    nvs_close(h);
    return n;
}

esp_err_t ac_store_event_append(const ac_event_record_t *r)
{
    uint32_t n = ac_store_event_count();
    char key[24];
    snprintf(key, sizeof(key), K_EVENTS "%lu", (unsigned long)(n % MAX_EVENTS));
    esp_err_t err = blob_save(key, r, sizeof(*r));
    if (err != ESP_OK) return err;
    nvs_handle_t h;
    err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, K_EVCOUNT, n + 1);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t ac_store_event_get(uint32_t index, ac_event_record_t *r)
{
    char key[24];
    snprintf(key, sizeof(key), K_EVENTS "%lu", (unsigned long)(index % MAX_EVENTS));
    return blob_load(key, r, sizeof(*r));
}

esp_err_t ac_store_erase_all(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "all stored state erased");
    return err;
}
