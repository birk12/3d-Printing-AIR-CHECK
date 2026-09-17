# Configuration

Nothing a user needs to change requires a firmware rebuild. Everything lives
in one NVS blob with a CRC, and everything is range-checked on the device by
`ac_config_validate()` before it is applied.

```bash
python3 tools/configuration/aircheck_config.py list
python3 tools/configuration/aircheck_config.py --port /dev/tty.usbmodem* get
python3 tools/configuration/aircheck_config.py --port ... set name "3D Printer"
```

## Identity

| setting | default | |
|---|---|---|
| `name` | `AIR CHECK` | shown on the display. Apple Home keeps its own name; this one is what distinguishes two units on their screens |
| `location` | empty | free text |

## Measurement

| setting | default | |
|---|---|---|
| `default_mode` | `ECO` | what the device falls back to. `NORMAL` if it stands next to a printer and you will charge it monthly |
| `profile[mode].pm_interval_s` | ECO 4 h, NORMAL 15 min | how often the SPS30 runs |
| `profile[mode].pm_window_s` | 40 s / 60 s | how long it runs. **Never below 8 s** - Sensirion say not to, and the validator enforces it |
| `profile[mode].voc_interval_s` | 10 s | clamped to 1-10 s, the range the Gas Index Algorithm is validated at |
| `profile[mode].co2_interval_s` | ECO 1 h | the SCD41's average current is a direct function of this |
| `profile[mode].icd_slow_poll_s` | 15 s | clamped to 15 s, the Matter SIT ICD limit |

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
| `ev_pm25_delta` | 5.0 ug/m3 | above baseline to arm |
| `ev_pm25_rate` | 1.5 ug/m3/min | rate of rise to arm |
| `ev_voc_delta` | 40 | VOC index points above baseline |
| `ev_confirm_s` | 120 s | how long a trigger must hold before the device commits |
| `ev_release_s` | 600 s | how long it must be clear before the event ends |
| `post_event_s` | 45 min | how long POST_PRINT runs |
| `auto_escalate` | true | switch to ACTIVE on a detected event |

Setting `ev_sensitivity` recomputes the four thresholds from the defaults, so
it is the one to reach for first.

## Baseline

| setting | default | |
|---|---|---|
| `baseline_update_s` | 900 s | |
| `baseline_alpha` | 0.02 | EMA weight per update; the time constant is `update_s / alpha`, about 12 hours at the defaults |

The baseline stops learning while an event is in progress. That is not
configurable, on purpose.

## Display

| setting | default | |
|---|---|---|
| `display_timeout_s` | 20 s | how long after a button press the device keeps refreshing. The image stays on screen either way - e-paper holds it at zero current |
| `display_start_screen` | 0 (overview) | |
| `display_show_delta` | true | show the delta against baseline on the particle screen |

## Battery

| setting | default | |
|---|---|---|
| `low_battery_pct` | 20 % | reported to Apple Home as a warning. In ECO that really is about a month of life left |
| `critical_battery_pct` | 5 % | measurement stops; only battery is still reported |

## Behaviour

| setting | default | |
|---|---|---|
| `voc_publish_index_as_ppb` | true | publish the VOC Index as a Matter number. The unit is wrong and is documented as such - see `docs/MATTER.md`. Set false for no number rather than a mislabelled one |
| `co2_self_calibration` | true | our substitute for the SCD41's own ASC, which power-cycled single-shot mode disables. See `docs/CALIBRATION.md` |

## Two units

The only settings worth differing between two devices:

```
unit A:  name = "3D Printer",  default_mode = NORMAL,  ev_sensitivity = 3
unit B:  name = "Room",        default_mode = ECO,     ev_sensitivity = 2
```

Same firmware binary on both. Nothing is compiled per unit.
