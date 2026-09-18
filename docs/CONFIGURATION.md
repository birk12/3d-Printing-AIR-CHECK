# Configuration

Nothing a user needs to change requires a firmware rebuild. Everything lives
in one NVS blob with a CRC, and everything is range-checked on the device by
`ac_config_validate()` before it is applied. The tool talks to a service
console on the USB-C port, which only runs while the cable is in.

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
| `default_mode` | `ECO` | what the device falls back to. `NORMAL` if it stands next to a printer and you will charge it every few weeks |

The per-mode profiles are compiled defaults in
`firmware/components/ac_core/src/ac_config.c`, not console settings - they
are the numbers the energy model is built on:

| profile field | default | |
|---|---|---|
| `pm_interval_s` | ECO 1 h, NORMAL 15 min | how often the SEN63C runs; since v1.2 that is also the CO2, temperature and humidity cadence. ECO at 4 h (14400) is the seven-month setting |
| `pm_window_s` | 40 s / 60 s | how long it runs. **Never below 30 s**: the SEN63C's CO2 output is "unknown" for the first 22-24 s, and the validator enforces the floor |
| `voc_interval_s` | 10 s | clamped to 1-10 s, the range the Gas Index Algorithm is validated at |
| `icd_slow_poll_s` | 15 s | clamped to 15 s, the Matter SIT ICD limit |

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
| `battery_interval_s` | 300 s | how often the cell voltage is read (ADC, microseconds of work). Also how quickly a plugged-in USB cable is noticed |

## Battery

| setting | default | |
|---|---|---|
| `low_battery_pct` | 20 % | reported to Apple Home as a warning. In ECO that really is about a month of life left |
| `critical_battery_pct` | 5 % | measurement stops; only battery is still reported |

## Behaviour

| setting | default | |
|---|---|---|
| `voc_publish_index_as_ppb` | true | publish the VOC Index as a Matter number. The unit is wrong and is documented as such - see `docs/MATTER.md`. Set false for no number rather than a mislabelled one |
| `co2_self_calibration` | true | the SEN63C's own CO2 self calibration. Stored in the sensor; the firmware writes it at boot and whenever this changes. See `docs/CALIBRATION.md` |

## Two units

The only settings worth differing between two devices:

```
unit A:  name = "3D Printer",  default_mode = ECO,  ev_sensitivity = 3
unit B:  name = "Room",        default_mode = ECO,  ev_sensitivity = 2
```

Same firmware binary on both. Nothing is compiled per unit.
