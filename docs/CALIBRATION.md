# Calibration and baselines

Two different things share the word "calibration" in air quality devices, and
confusing them is how people end up with a sensor that reads nonsense.

| | **baseline reset** | **sensor calibration** |
|---|---|---|
| what it touches | this firmware's own reference numbers | the sensor's internal calibration |
| what it changes | what "normal for this room" means | what the sensor reports |
| reversible | yes, instantly | only by recalibrating again |
| how | button, or a config command | deliberate, documented, guarded |

The firmware keeps them strictly apart. `ac_baseline_reset()` never writes to a
sensor. It is a one-line rule in the code and it is worth keeping.

## What is factory calibrated, and what is not

| sensor | calibration |
|---|---|
| **SPS30** | factory calibrated against a TSI DustTrak DRX 8533 for mass and a TSI OPS 3330 for number concentration. No field calibration exists or is needed. Drift is specified at up to 1.25 ug/m3 per year. |
| **SGP40** | produces a *raw* signal; the VOC Index is computed on our side by Sensirion's algorithm, which self-calibrates continuously against the last 24 hours. There is nothing to calibrate. |
| **SCD41** | factory calibrated, but NDIR CO2 sensors drift, which is why the part has an automatic self-calibration mode. See below - this is the one that needs attention. |

## The CO2 problem, and what this firmware does about it

The SCD41 is run in **power-cycled single-shot mode**, because that is the only
mode whose current (43 uA at a one hour cadence) fits the energy budget. The
datasheet is explicit about the consequence:

> "for power-cycled single shot operation, ASC is not available in either case"

So the firmware implements the equivalent itself:

1. It tracks the **lowest CO2 reading over a rolling seven day window**.
2. If that minimum is stable, and the implied correction is **less than
   100 ppm**, it issues a forced recalibration against **420 ppm** - today's
   outdoor background.
3. If the correction would be larger than that, it does nothing and logs it.
   A large correction means either the room genuinely never gets fresh air, or
   something is wrong, and neither is a good reason to rewrite the sensor's
   calibration.

This is what Sensirion's own ASC does internally. It rests on one assumption:
**the room reaches something close to outdoor air at least once a week.**

### When to turn it off

Set `co2_self_calibration = false` if:

* the room is genuinely never ventilated (a sealed basement, a grow tent)
* the device lives somewhere with a permanently raised CO2 background
* you calibrate manually and would rather the firmware left it alone

With it off, expect the CO2 reading to drift over months. Relative changes -
"CO2 went up 400 ppm while the printer ran" - stay meaningful for far longer
than absolute values do.

### Manual forced recalibration

Take the device outdoors, or to an open window, let it sit for at least three
minutes, then trigger a forced recalibration against 420 ppm. The command is in
`tools/diagnostics/`. Do not do this indoors: you will calibrate the sensor to
whatever the room happens to be, and every reading afterwards will be wrong by
that much.

## Temperature offset

The SCD41 reports the temperature of its own package, which sits inside a
sealed box. Even with the sensor bay walled off from the electronics and the
battery, it will read high.

The default offset is 4 degC, which is the datasheet default and assumes the
sensor is next to a warm board. This design puts it in a ventilated bay
*below* everything that produces heat, so the real offset is probably lower.

**Measure it.** Let the device run for two hours in a room with a known
temperature, compare, and set the difference:

```
measured_offset = reading - actual
new_offset = current_offset - measured_offset
```

Until you do, treat the temperature as an indicator, not a thermometer. The
humidity reading depends on it, and so does the SGP40's humidity compensation,
so this is worth ten minutes.

## Baseline reset - "set room air as baseline"

The baseline is the device's idea of what this room normally looks like. It
learns continuously, slowly, and it **stops learning while an event is in
progress** so that a four-hour print does not quietly become the new normal.

Reset it when:

* the device has moved to a different room
* you have done something that permanently changed the room - a new filter, a
  new extractor, the printer moved out
* the device spent its first days learning a baseline during a print, and now
  thinks a printing workshop is normal

Do **not** reset it just because a number looks high. That is the baseline
doing its job.

After a reset the device adopts the current air immediately as its reference,
and event detection is effectively disabled until a few samples have gone by -
the detector refuses to fire without a valid baseline, on purpose.

## What this device is not

It is not a reference instrument and it is not a safety device. The SPS30 is
calibrated against a specific optical reference under specific conditions; the
VOC Index is a relative indicator that cannot name a single chemical; the
thresholds in this firmware are reporting thresholds chosen to be useful, not
health limits.

The right way to use it is **comparatively**: this room against that room, now
against an hour ago, with the printer running against with it off. Those
comparisons are exactly what it is good at.
