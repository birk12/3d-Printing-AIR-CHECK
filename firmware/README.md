# Firmware

ESP-IDF v5.5.x + esp-matter release/v1.6, target **esp32c6**.

## Layout

```
components/ac_core/    platform-independent measurement core - no esp_* headers
components/ac_hal/     ESP-IDF drivers
components/sensirion_gas_index/   vendored VOC Index algorithm (BSD-3)
main/                  app_main.cpp and the Matter data model
test/host/             455 checks that run on a workstation
```

The split is the point: `ac_core` decides what happens and when, `ac_hal`
only carries it out. Everything interesting is therefore testable without
hardware.

## Building

```bash
. $HOME/esp/esp-idf/export.sh
. $HOME/esp/esp-matter/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/tty.usbmodem* flash monitor
```

`ESP_MATTER_PATH` must be set; the top-level `CMakeLists.txt` says so if it is
not. Note that `esp-matter/examples/common` is deliberately **not** on
`EXTRA_COMPONENT_DIRS` - it pulls in helper components that depend on
`espressif/button` from the registry, and this project has its own button
handling.

The first build takes a while: it compiles the whole Matter SDK.

## Host tests

No ESP-IDF needed:

```bash
cc -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/components/ac_core/include \
   components/ac_core/src/*.c test/host/test_ac_core.c -lm -o /tmp/ac_test
/tmp/ac_test
```

About 20 ms. See `docs/TESTING.md` for what they cover.

## Screen previews

```bash
cc -std=c99 -I components/ac_core/include components/ac_core/src/*.c \
   ../tools/diagnostics/screen_preview.c -lm -o /tmp/preview
/tmp/preview /tmp/screens
```

Writes every screen as a PBM, exactly as the panel would show it.

## Configuration

`sdkconfig.defaults` is the shipped configuration - Thread only, Wi-Fi
compiled out, SIT ICD at a 15 s poll, light sleep on, no CLI.
`sdkconfig.defaults.lit` switches to a Long Idle Time ICD and is experimental;
see `docs/MATTER.md` for why.

Runtime settings - names, thresholds, intervals, sensitivity - are in NVS and
never need a rebuild. `ac_config_validate()` clamps everything into the range
the sensor datasheets allow, so a bad value cannot produce a schedule that the
energy model did not account for.

## Fonts

`ac_font_data.c` is generated:

```bash
python3 ../tools/fontgen/make_font.py
```

Three faces rasterised from DejaVu Sans: 12 px regular, 20 px bold, 46 px bold
digits. About 60 kB of source, 25 kB of flash.

## Vendored code

`components/sensirion_gas_index` is Sensirion's Gas Index Algorithm, verbatim,
BSD-3-Clause. Refresh with `tools/fetch_vendor.sh`; do not edit it in place.
