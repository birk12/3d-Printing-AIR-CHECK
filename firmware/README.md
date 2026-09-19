# Firmware

ESP-IDF v5.5.x + esp-matter release/v1.6, target **esp32c6**.

## Layout

```
components/ac_core/    platform-independent measurement core - no esp_* headers
components/ac_hal/     ESP-IDF drivers: SEN62, Sunrise, SGP40, SHT40, AA pack and VSYS ADC, LED, button, NVS
components/sensirion_gas_index/   vendored VOC Index algorithm (BSD-3)
main/                  app_main.cpp and the Matter data model
test/host/             546 checks that run on a workstation
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

The first build takes a while: it compiles the whole Matter SDK. The result
(v1.3.1):

```
aircheck.bin   1 686 944 bytes, 14 % free in the 1.9 MB OTA partition
DIRAM          210 208 bytes, 46.5 % of 452 112
```

Almost half the RAM is gone before anything is allocated at runtime. That is
the measured reason this is an ESP32-C6 and not an ESP32-H2 (EDR-1 in
`docs/ENGINEERING_DECISIONS.md`).

## Host tests

No ESP-IDF needed:

```bash
cc -std=c99 -Wall -Wextra -Werror -O1 -Icomponents/ac_core/include \
   components/ac_core/src/*.c test/host/test_ac_core.c -lm -o /tmp/ac_test
/tmp/ac_test
```

546 checks, about 20 ms. See `docs/TESTING.md` for what they cover.

## Power source (1.3.1)

`ac_power_classify()` in `ac_core/ac_battery.c` tells the cells from external
power by VSYS on GPIO0 (the FireBeetle's 1M/1M divider): at or above
`AC_EXT_POWER_V` (4.08 V), or with an enumerated USB host, the device is on
external power; a pack below `AC_PACK_ABSENT_V` (3.0 V) then means an empty
holder. External power means CONTINUOUS mode, no low/critical battery state,
and only the regulator's quiescent current and the resistors across the pack
booked against the cells. The log prints `power: cells`, `power: external,
cells as backup` or `power: external, no cells` with VSYS and the pack
voltage on each change; Matter Power Source `Status` is 1, 2 or 3
accordingly (`docs/MATTER.md`).

## Configuration

`sdkconfig.defaults` is the shipped configuration - Thread only, Wi-Fi
compiled out, SIT ICD at a 15 s poll, light sleep on, no CLI.
`sdkconfig.defaults.lit` switches to a Long Idle Time ICD and is experimental;
see `docs/MATTER.md` for why.

Runtime settings - names, thresholds, sensitivity, cell type, altitude - are
in NVS and never need a rebuild (`docs/CONFIGURATION.md`). `ac_config_validate()` clamps everything into the range
the sensor datasheets allow, so a bad value cannot produce a schedule that the
energy model did not account for.

## Vendored code

`components/sensirion_gas_index` is Sensirion's Gas Index Algorithm, verbatim,
BSD-3-Clause. Refresh with `tools/fetch_vendor.sh`; do not edit it in place.
