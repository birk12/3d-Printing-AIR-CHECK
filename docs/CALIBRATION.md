# Calibration and baselines

Two different things share the word "calibration" in air quality devices, and
confusing them is how people end up with a sensor that reads nonsense.

| | **baseline reset** | **sensor calibration** |
|---|---|---|
| what it touches | this firmware's own reference numbers | the sensor's internal calibration |
| what it changes | what "normal for this room" means | what the sensor reports |
| reversible | yes, instantly | only by recalibrating again |
| how | `aircheck_config.py baseline-reset` over USB | fresh-air calibration: button held 8-12 s outdoors, or `aircheck_config.py frc` |

The firmware keeps them strictly apart. `ac_baseline_reset()` never writes to a
sensor. It is a one-line rule in the code and it is worth keeping.

## What is factory calibrated, and what is not

| sensor | calibration |
|---|---|
| **SEN63C, particles** | factory calibrated; Sensirion calibrate the SEN6x's PM2.5 output to a TSI DustTrak DRX 8533 in ambient mode. No field calibration exists or is needed. Long-term drift is specified at up to 1.25 ug/m3 per year. |
| **SEN63C, CO2** | factory calibrated, ±(100 ppm + 10 %) after 12 h of operation followed by fresh air. CO2 sensors drift, which is why the module has automatic self calibration (ASC). See below - this is the one that needs attention. |
| **SEN63C, temperature and humidity** | factory calibrated, and compensated for the module's own self-heating by Sensirion's firmware ("STAR engine"). |
| **SGP40** | produces a *raw* signal; the VOC Index is computed on our side by Sensirion's algorithm, which self-calibrates continuously against the last 24 hours. There is nothing to calibrate. |

## CO2: self calibration, and the fallback

The SEN63C's own **automatic self calibration (ASC)** is on by default, and
the setting is stored inside the sensor. The firmware writes it at boot from
`co2_self_calibration`, so the configuration is the single source of truth.

ASC assumes what all CO2 self calibration assumes: **the room reaches
something close to outdoor air at least once a week**, and the sensor sees it.
The open question for this device is the second half. In ECO the SEN63C runs
40 s an hour, and Sensirion do not document whether ASC converges on so little
running time. Until bench test B16 has answered that, treat the absolute CO2
number with some suspicion and the relative one - "up 400 ppm while the
printer ran" - with confidence.

**v1.0 and v1.1 said the firmware implemented its own ASC. It did not**: the
flag existed, no code used it. That text is gone.

### Fresh-air calibration (forced recalibration)

If the CO2 reading has drifted - a well-aired room that never reads below
500 ppm, or two units that disagree by more than their spec - calibrate it
against outdoor air:

1. Take the device **outdoors**, or put it in a wide-open window, away from
   people, cars and chimneys.
2. Hold the button **8 to 12 seconds**. While you hold it the LED turns blue at
   3 s (pairing) and **cyan at 8 s: let go while it is cyan.**
3. The LED blinks cyan slowly for three minutes while the SEN63C runs in the
   fresh air, then shows **green** (done) or **fast red** (failed - usually the
   sensor did not answer; try again).
4. Bring it back in.

The reference is 425 ppm, today's outdoor background. With a cable, the same
thing is `aircheck_config.py frc 425`, or `co2 frc 425` on the console.

**Do not do this indoors.** You would calibrate the sensor to whatever the
room happens to be, and every reading afterwards would be wrong by that much.

### When to turn ASC off

Set `co2_self_calibration = false` if:

* the room is genuinely never ventilated (a sealed basement, a grow tent)
* the device lives somewhere with a permanently raised CO2 background
* you calibrate with fresh air now and then and would rather ASC left it alone

## Temperature and humidity

The SEN63C compensates for its own heating, and in this case it sits against
the outer wall, below everything else that could get warm, with its own air
path. It runs 40 s an hour, so there is little self-heating to begin with.
What it cannot know about is the case; if the reading is off against a
thermometer you trust, note the offset. There is no offset setting yet - the
SEN6x has one (`Set Temperature Offset Parameters`), and wiring it to the
configuration is an open item, not a feature.

The SGP40's humidity compensation uses these values, so they matter beyond
the temperature tile in Apple Home.

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

It is not a reference instrument and it is not a safety device. The SEN63C
is calibrated against a specific optical reference under specific conditions; the
VOC Index is a relative indicator that cannot name a single chemical; the
thresholds in this firmware are reporting thresholds chosen to be useful, not
health limits.

The right way to use it is **comparatively**: this room against that room, now
against an hour ago, with the printer running against with it off. Those
comparisons are exactly what it is good at.
