# Firmware

ESP-IDF v5.5.x + esp-matter release/v1.6, target **esp32c6**.

## Layout

```
components/ac_core/    platform-independent measurement core - no esp_* headers
components/pwr_std/    the Power-Standard's LFP module (C99), copied unchanged
components/ac_hal/     ESP-IDF drivers: SEN62, Sunrise, SGP40, SHT40, the VBAT_S / PWR-K / VSYS ADC, CE, LED, button, NVS
components/sensirion_gas_index/   vendored VOC Index algorithm (BSD-3)
main/                  app_main.cpp and the Matter data model
test/host/             543 checks plus the pwr_std test, run on a workstation
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
(v1.5.0, ESP-IDF v5.5.5):

```
aircheck.bin   1 687 776 bytes, 14 % free in the 1.9 MB OTA partition
DIRAM          210 224 bytes, 46.5 % of 452 112
```

Almost half the RAM is gone before anything is allocated at runtime. That is
the measured reason this is an ESP32-C6 and not an ESP32-H2 (EDR-1 in
`docs/ENGINEERING_DECISIONS.md`).

## Host tests

No ESP-IDF needed:

```bash
cc -std=c99 -Wall -Wextra -Werror -O1 -Icomponents/ac_core/include \
   -Icomponents/pwr_std/include \
   components/ac_core/src/*.c components/pwr_std/src/pwr_std.c \
   test/host/test_ac_core.c -lm -o /tmp/ac_test
/tmp/ac_test
cc -std=c99 -Wall -Wextra -Werror -Icomponents/pwr_std/include \
   test/host/test_pwr_std.c components/pwr_std/src/pwr_std.c -o /tmp/pwr_test
/tmp/pwr_test
```

543 checks, about 20 ms, then `all tests passed` from `pwr_std`. See
`docs/TESTING.md` for what they cover.

## Power (1.5.0)

The Power-Standard's module C (EDR-21): a USB-C socket, the #6091 charger
(TI BQ25185), 1S4P LiFePO4. `ac_hal/battery.c` reads the cell voltage /
2 on GPIO3 (ADC 6 dB), the PWR-K ladder on GPIO4 (ADC 12 dB, two readings
50 ms apart) and VSYS on GPIO0 (sanity only), and drives CE on GPIO5 - only
ever high (charge pause) or an input (charging allowed; the #6091's
pull-down is the fail-safe). `ac_core/ac_power.c` wraps `pwr_std`:

* **External power** is PWR-K EXT (USB-C in J2) or an enumerated USB host on
  the FireBeetle: CONTINUOUS mode, no low/critical battery state.
* **Charge pause** (`pwr_hold_update()`): after "full" on USB-C, CE is held
  high until USB-C is unplugged, the cells fall below 3.30 V, or 30 days
  have passed.
* **Safety timer** (`pwr_timer_retry()`): 1S4P needs 8–9 h from flat, the
  BQ25185 stops after 6 h. PWR-K cannot tell its latched fault from a
  recoverable one, so a fault seen after at least 5.5 h of charging
  (`AC_TIMER_SUSPECT_S`) is taken for the timer and gets one 150 ms CE pulse
  per USB session. A second one stays latched.
* **Levels** from the cell voltage: warning below 3.20 V (about 10 %),
  critical below 3.10 V (about 6 %, measuring stops), 0.05 V hysteresis. The
  percentage is coarse, from the LFP voltage curve.

On every change of source, charge state or fault the log prints

```
power: cells|USB-C|USB-C, no cells, cells X.XX V (N %), charging|full|not charging[, charge paused][, FAULT (timer?)|, fault (temperature/OVP)], VSYS X.XX V
```

and, after a timer restart, `safety timer ran out before full: charging
restarted once`. The console's `diag` shows `LFP cells X.XX V, N %,
charging|not charging, external power yes|no`. `ac_matter_publish_power()`
updates the battery Power Source on endpoint 0 (`Status`, `BatPresent`,
`BatPercentRemaining`, `BatVoltage`, `BatChargeLevel`, `BatChargeState`,
`BatReplacementNeeded`) and the USB-C Power Source on its own endpoint
(`Status`, `WiredPresent`); see `docs/MATTER.md`. After a reboot the charge
pause starts released and `pwr_std` learns "full" again.

## Configuration

`sdkconfig.defaults` is the shipped configuration - Thread only, Wi-Fi
compiled out, SIT ICD at a 15 s poll, light sleep on, no CLI.
`sdkconfig.defaults.lit` switches to a Long Idle Time ICD and is experimental;
see `docs/MATTER.md` for why.

Runtime settings - names, thresholds, sensitivity, altitude - are in NVS
(configuration version 5) and never need a rebuild
(`docs/CONFIGURATION.md`). There are no battery settings; the power
module's thresholds are fixed in `pwr_std`. `ac_config_validate()` clamps everything into the range
the sensor datasheets allow, so a bad value cannot produce a schedule that the
energy model did not account for.

## Vendored code

`components/sensirion_gas_index` is Sensirion's Gas Index Algorithm, verbatim,
BSD-3-Clause. Refresh with `tools/fetch_vendor.sh`; do not edit it in place.
