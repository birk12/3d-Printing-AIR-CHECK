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
| **SEN62, particles** | factory calibrated; Sensirion calibrate the SEN6x's PM2.5 output to a TSI DustTrak DRX 8533 in ambient mode. No field calibration exists or is needed. Long-term drift is specified at up to 1.25 ug/m3 per year. |
| **Senseair Sunrise, CO2** | NDIR, ±(30 ppm + 3 %). CO2 sensors drift, which is why it runs automatic baseline correction (ABC). See below - this is the one that needs attention. |
| **SHT40, temperature and humidity** | ±0.2 °C, ±1.8 %RH. Nothing to calibrate. |
| **SGP40** | produces a *raw* signal; the VOC Index is computed on our side by Sensirion's algorithm, which self-calibrates continuously against the last 24 hours. There is nothing to calibrate. |

## Altitude - set it once

NDIR reads molecules per volume, so CO2 depends on air pressure: 1.6 % per
kPa. The Sunrise takes a pressure value with every measurement. There is no
barometer in the device, so the firmware computes the station pressure from
the configured altitude (ISA formula):

```bash
python3 tools/configuration/aircheck_config.py --port ... set altitude_m 520
```

Range -400 to 4000 m, default 0. The boot log shows the result
(`site 520 m = ... hPa`). Weather still moves the real pressure by about
±2 kPa, which is about ±3 % of reading. That is the one known residual, and
it is inside the sensor's own ±3 % term (EDR-16).

## CO2: self calibration, and the fallback

The Sunrise's **ABC** is on by default: a 180 h period and a 425 ppm target,
today's outdoor background. The sensor is switched off between measurements,
so it cannot keep its own ABC state; the firmware does. After every
measurement it reads the ABC and filter state, writes it back before the next
one, and adds the hours that have passed to the ABC clock, as Senseair's
integration guide requires. The state is stored in NVS (blob `sunrise`), so
it survives a reboot and a battery change.

`co2_self_calibration` switches ABC on or off. It lives in the sensor's
EEPROM; the firmware writes it at boot and whenever the configuration
changes, and only if it differs, so the configuration is the single source of
truth.

ABC assumes what all CO2 self calibration assumes: **the room reaches
something close to outdoor air at least once a week**, and the sensor sees it.
The Sunrise is built for this single-measurement duty cycle, unlike the
SEN63C that v1.2 used (EDR-16). The host-held state has not yet been checked (TESTING T-S3, T-S7)
on an assembled unit, so until it has, treat the absolute CO2 number with some
suspicion and the relative one - "up 400 ppm while the printer ran" - with
confidence.

**v1.0 and v1.1 said the firmware implemented its own ASC. It did not**: the
flag existed, no code used it. That text is gone.

### Fresh-air calibration (target calibration)

If the CO2 reading has drifted - a well-aired room that never reads below
500 ppm, or two units that disagree by more than their spec - calibrate it
against outdoor air:

1. Take the device **outdoors**, or put it in a wide-open window, away from
   people, cars and chimneys.
2. Hold the button **8 to 12 seconds**. While you hold it the LED turns blue at
   3 s (pairing) and **cyan at 8 s: let go while it is cyan.**
3. The LED blinks cyan slowly for up to four minutes: three ordinary
   measurements a minute apart, so the sensor sits in the fresh air, then a
   Senseair target calibration. Then it shows **green** (done) or **fast
   red** (failed - the sensor did not answer or did not confirm the
   calibration; try again).
4. Bring it back in.

The reference is 425 ppm. With a cable, the same thing is
`aircheck_config.py frc 425`, or `co2 frc 425` on the console (400 to
2000 ppm accepted).

**Do not do this indoors.** You would calibrate the sensor to whatever the
room happens to be, and every reading afterwards would be wrong by that much.

### When to turn ABC off

Set `co2_self_calibration = false` if:

* the room is genuinely never ventilated (a sealed basement, a grow tent)
* the device lives somewhere with a permanently raised CO2 background
* you calibrate with fresh air now and then and would rather ABC left it alone

## Temperature and humidity

The SHT40 sits low in the gas bay, next to the vents, furthest from the
ESP32 and the Sunrise's lamp (EDR-17), and is read every 10 s (one
measurement takes at most 8.3 ms). What it cannot know about is the case;
if the reading is off against a thermometer you trust, note the offset.
There is no offset setting.

While the cells charge, the charger in its chamber at the bottom of the case
turns 0.85–1.7 W into heat, and temperature and humidity can read a little
high. Nothing is compensated for that; Matter's `BatChargeState` says
IsCharging for exactly that window (`docs/DASHBOARD_INTERFACE.md`). Compare
against your reference thermometer outside it - on permanent USB-C the
firmware pauses charging after "full" and tops up only about once a month.
TESTING T-L7 measures the offset while charging.

The SGP40's humidity compensation uses these values with every sample, so
they matter beyond the temperature tile in Apple Home.

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

## The cell voltage - one point, once per device

Not a sensor, but it is calibrated the same way and it is the one measurement
that decides when the device warns and when it goes to sleep.

The cells are read through a 470k/470k divider on GPIO3. Two errors add up:
the resistors' 1 % and the ESP32-C6's own ADC error of +-23 mV at 6 dB
attenuation (datasheet v1.5, Table 5-6), which the divider doubles into
+-46 mV on the cell. Together that is +-77 mV - more than the 50 mV
hysteresis and more than half the distance from the warning level (3.20 V) to
the critical one (3.10 V). Both terms are gains, so one measured point takes
most of them out:

```
cal battery 3.28        # what a multimeter reads at the cells, in volts
cal show                # the stored factor
cal battery 0           # back to the nominal ratio
```

Do it during assembly, when the meter is on the cells anyway (test protocol
PS-4.1, acceptance T-L1c). The factor survives a power cycle, is refused
outside 0.95-1.05, and shows up in `diag`. This is the electronics audit's
NC-03, and it is the reason the runtime figures and the Matter battery level
can be trusted to about 20 mV instead of 77.

## What this device is not

It is not a reference instrument and it is not a safety device. The SEN62
is calibrated against a specific optical reference under specific conditions; the
VOC Index is a relative indicator that cannot name a single chemical; the
thresholds in this firmware are reporting thresholds chosen to be useful, not
health limits.

The right way to use it is **comparatively**: this room against that room, now
against an hour ago, with the printer running against with it off. Those
comparisons are exactly what it is good at.
