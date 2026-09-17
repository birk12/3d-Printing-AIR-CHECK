/* 1.54 in 200 x 200 e-paper, SSD1681 controller (Waveshare module / Good
 * Display GDEY0154D67 panel).
 *
 * Bistable: the image costs nothing to hold, only to change.  That is the
 * whole reason for choosing e-paper here - the screen can stay readable for
 * six months while the display budget in docs/BATTERY_LIFE.md stays at
 * 0.03 mAh a day.
 */
#include "ac_hal/ac_hal.h"
#include "ac_core/ac_display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "epd";
static spi_device_handle_t s_spi;

#define EPD_W 200
#define EPD_H 200

static void dc(int level) { gpio_set_level(AC_PIN_EPD_DC, level); }

static esp_err_t spi_write(const uint8_t *data, size_t n)
{
    if (!n) return ESP_OK;
    spi_transaction_t t = { .length = n * 8, .tx_buffer = data };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t send_cmd(uint8_t c)
{
    dc(0);
    gpio_set_level(AC_PIN_EPD_CS, 0);
    esp_err_t err = spi_write(&c, 1);
    gpio_set_level(AC_PIN_EPD_CS, 1);
    return err;
}

static esp_err_t send_data(const uint8_t *d, size_t n)
{
    dc(1);
    gpio_set_level(AC_PIN_EPD_CS, 0);
    esp_err_t err = spi_write(d, n);
    gpio_set_level(AC_PIN_EPD_CS, 1);
    return err;
}

static esp_err_t send_byte(uint8_t b) { return send_data(&b, 1); }

static esp_err_t wait_idle(uint32_t timeout_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (gpio_get_level(AC_PIN_EPD_BUSY)) {
        if (xTaskGetTickCount() > deadline) {
            ESP_LOGE(TAG, "BUSY stuck high");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

static void hw_reset(void)
{
    gpio_set_level(AC_PIN_EPD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(AC_PIN_EPD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t ac_epd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << AC_PIN_EPD_DC) | (1ULL << AC_PIN_EPD_CS) |
                        (1ULL << AC_PIN_EPD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_config_t busy = {
        .pin_bit_mask = 1ULL << AC_PIN_EPD_BUSY,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* the board has 100k as well */
    };
    ESP_ERROR_CHECK(gpio_config(&busy));
    gpio_set_level(AC_PIN_EPD_CS, 1);

    spi_bus_config_t bus = {
        .mosi_io_num = AC_PIN_EPD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = AC_PIN_EPD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = AC_DISP_BYTES + 16,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,        /* CS driven by hand, the panel needs it
                                    * held across a whole command + data run */
        .queue_size = 1,
    };
    return spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
}

static esp_err_t panel_setup(bool fast)
{
    hw_reset();
    if (wait_idle(2000) != ESP_OK) return ESP_ERR_TIMEOUT;

    send_cmd(0x12);                       /* SWRESET */
    if (wait_idle(2000) != ESP_OK) return ESP_ERR_TIMEOUT;

    send_cmd(0x01);                       /* driver output control */
    send_byte((EPD_H - 1) & 0xFF);
    send_byte(((EPD_H - 1) >> 8) & 0xFF);
    send_byte(0x00);

    send_cmd(0x11);                       /* data entry mode: X+, Y+ */
    send_byte(0x03);

    send_cmd(0x44);                       /* RAM X window */
    send_byte(0x00);
    send_byte((EPD_W / 8) - 1);
    send_cmd(0x45);                       /* RAM Y window */
    send_byte(0x00); send_byte(0x00);
    send_byte((EPD_H - 1) & 0xFF); send_byte(((EPD_H - 1) >> 8) & 0xFF);

    send_cmd(0x3C);                       /* border waveform */
    send_byte(0x05);

    send_cmd(0x18);                       /* read built-in temperature sensor */
    send_byte(0x80);

    send_cmd(0x21);                       /* display update control 1 */
    send_byte(0x00); send_byte(0x80);

    send_cmd(0x4E); send_byte(0x00);      /* RAM X counter */
    send_cmd(0x4F); send_byte(0x00); send_byte(0x00);
    (void)fast;
    return wait_idle(2000);
}

static esp_err_t write_ram(uint8_t reg, const uint8_t *fb)
{
    /* The canvas stores 1 = black; the SSD1681 wants 0 = black. */
    static uint8_t line[AC_DISP_STRIDE];
    send_cmd(reg);
    for (int y = 0; y < EPD_H; y++) {
        for (int i = 0; i < AC_DISP_STRIDE; i++)
            line[i] = (uint8_t)~fb[y * AC_DISP_STRIDE + i];
        esp_err_t err = send_data(line, AC_DISP_STRIDE);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t ac_epd_full_update(const uint8_t *fb)
{
    esp_err_t err = panel_setup(false);
    if (err != ESP_OK) return err;
    err = write_ram(0x24, fb);              /* black/white RAM */
    if (err != ESP_OK) return err;
    err = write_ram(0x26, fb);              /* previous image, stops ghosting */
    if (err != ESP_OK) return err;
    send_cmd(0x22); send_byte(0xF7);        /* full update sequence */
    send_cmd(0x20);
    return wait_idle(6000);
}

esp_err_t ac_epd_partial_update(const uint8_t *fb)
{
    esp_err_t err = panel_setup(true);
    if (err != ESP_OK) return err;
    err = write_ram(0x24, fb);
    if (err != ESP_OK) return err;
    send_cmd(0x22); send_byte(0xFF);        /* partial update sequence */
    send_cmd(0x20);
    err = wait_idle(3000);
    if (err != ESP_OK) return err;
    /* keep the controller's "previous image" buffer in step, otherwise the
     * next partial update leaves the old pixels behind */
    return write_ram(0x26, fb);
}

esp_err_t ac_epd_sleep(void)
{
    send_cmd(0x10);
    send_byte(0x01);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}
