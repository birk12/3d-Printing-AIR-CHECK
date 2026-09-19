# Configuration

Nothing a user needs to change requires a firmware rebuild. Everything lives
in one NVS blob with a CRC, and everything is range-checked on the device by
`ac_config_validate()` before it is applied. The tool talks to a service
console on the USB-C port, which only runs while a computer is connected.

```bash
python3 tools/configuration/aircheck_config.py list
python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* get
python3 tools/configuration/aircheck_config.py --port ... set name "3D Printer"
```

## Identity

| setting | default | |
|---|---|---|
| `name` | `AIR CHECK` | published as Matter NodeLabel. Apple Home keeps its own name; this is the one the dashboard sees, so it is what tells two units apart there |
| `location` | empty | free text |

## Measurement

| setting | default | |
|---|---|---|
| `default_mode` | `ECO` | what the device falls back to. `NORMAL` if it stands next to a printer and you will change the cells about every 5 weeks (L91) |

The per-mode profiles are compiled defaults in
`firmware/components/ac_core/src/ac_config.c`, not console settings - they
are the numbers the energy model is built on:

| profile field | default | |
|---|---|---|
| `pm_interval_s` | ECO 1 h, NORMAL 15 min, ACTIVE 2 min, POST_PRINT 5 min, CONTINUOUS 0 (always on) | how often the SEN62 runs. ECO at 2 h (7200) gives 5.6 months on L91 cells |
| `pm_window_s` | 60 s | how long it runs. **Never below 60 s**: the first 30 s are discarded while the SEN62 settles, the rest is averaged; the validator enforces the floor |
| `co2_interval_s` | 5 min; ACTIVE 2 min, CONTINUOUS 1 min | one Sunrise single measurement. Floor 60 s. Costs almost nothing: every 10 min instead of 5 makes no visible difference to the runtime |
| `voc_interval_s` | 10 s; CONTINUOUS 1 s | SGP40, with the SHT40's temperature and humidity read just before it. Clamped to 1-10 s, the range the Gas Index Algorithm is validated at |
| `icd_slow_poll_s` | 15 s; ACTIVE, POST_PRINT, CONTINUOUS 5 s | clamped to 15 s, the Matter SIT ICD limit. The energy model uses it; the firmware does not apply it at runtime - the radio polls at the 15 s set in `sdkconfig.defaults` |

CONTINUOUS is used automatically while a computer is connected over USB; the
cells are not charged meanwhile (EDR-18).

Change any of these and re-run the energy model before believing the runtime:

```bash
python3 tools/battery_calculator/model.py
```

## Thresholds

| setting | default | |
|---|---|---|
| `pm25_elevated` / `_high` / `_very_high` | 15 / 35 / 75 ug/m3 | the WHO 2021 24 h guideline is 15 and interim target 1 is 75; these are the anchors |
| `pm10_elevated` / `_high` / `_very_high` | 45 / 100 / 150 ug/m3 | |
| `voc_elevated` / `_high` / `_very_high` | 150 / 250 / 350 | VOC Index points, where 100 is the 24 h average |
| `co2_elevated` / `_high` / `_very_high` | 1000 / 1500 / 2000 ppm | |

These are **reporting** thresholds for a consumer device, not health limits.
The validator keeps them ordered: setting `high` below `elevated` is corrected
rather than accepted.

## Event detection

| setting | default | |
|---|---|---|
| `ev_sensitivity` | 3 | a 1-5 dial. 3 reproduces the built-in thresholds exactly; 1 needs about three times the excursion, 5 about half |
| `ev_pm25_delta` | 5.0 ug/m3 | above baseline to arm (set through `ev_sensitivity`) |
| `ev_pm25_rate` | 1.5 ug/m3/min | rate of rise to arm (set through `ev_sensitivity`) |
| `ev_voc_delta` | 40 | VOC index points above baseline (set through `ev_sensitivity`) |
| `ev_confirm_s` | 120 s | how long a trigger must hold before the device commits |
| `ev_release_s` | 600 s | how long it must be clear before the event ends |
| `post_event_s` | 45 min | how long POST_PRINT runs |
| `auto_escalate` | true | switch to ACTIVE on a detected event |

Setting `ev_sensitivity` recomputes the four thresholds from the defaults, so
it is the one to reach for first.

## Baseline

| setting | default | |
|---|---|---|
| `baseline_update_s` | 900 s | compiled default |
| `baseline_alpha` | 0.02 | compiled default; EMA weight per update, the time constant is `update_s / alpha`, about 12 hours |

The baseline stops learning while an event is in progress. That is not
configurable, on purpose.

## Status LED and battery

| setting | default | |
|---|---|---|
| `led_show_air_quality` | true | a short button press shows the air-quality colour for 3 s. Off: it only blinks green once, as a sign of life |
| `battery_interval_s` | 300 s | how often the pack voltage is read (60-3600 s). Also how quickly a connected computer is noticed |

## Battery

| setting | default | |
|---|---|---|
| `cell_type` | `alkaline` | `alkaline`, `nimh` or `lithium` (Energizer L91 class): selects the voltage curve and the usable energy for the battery percentage. Set it to what is in the compartment |
| `cells` | 6 | AA cells in series, 1-8. The holder takes 6 |
| `low_battery_pct` | 20 % | 5-50 %. Reported to Apple Home as a warning |
| `critical_battery_pct` | 5 % | 1-20 %. Measurement stops; only battery is still reported |

The percentage is the lower of two estimates: the chemistry's voltage curve
and an energy counter of what the firmware has spent. Fresh cells are
recognised by their voltage jump and restart the counter; changing
`cell_type` or `cells` restarts it too.

## Site

| setting | default | |
|---|---|---|
| `altitude_m` | 0 | -400 to 4000 m. The CO2 reading is compensated for air pressure computed from it (no barometer on board). See `docs/CALIBRATION.md` |

## Behaviour

| setting | default | |
|---|---|---|
| `voc_publish_index_as_ppb` | true | publish the VOC Index as a Matter number. The unit is wrong and is documented as such - see `docs/MATTER.md`. Set false for no number rather than a mislabelled one |
| `co2_self_calibration` | true | the Sunrise's ABC (180 h period, 425 ppm target). Stored in the sensor; the firmware writes it at boot and whenever the configuration changes, if it differs. See `docs/CALIBRATION.md` |

## Two units

The only settings worth differing between two devices:

```
unit A:  name = "3D Printer",  default_mode = ECO,  ev_sensitivity = 3
unit B:  name = "Room",        default_mode = ECO,  ev_sensitivity = 2
```

Same firmware binary on both. Nothing is compiled per unit.
