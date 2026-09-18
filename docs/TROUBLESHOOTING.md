# Troubleshooting

Work down the list. Almost everything is one of the first five.

## The LED does nothing

| check | |
|---|---|
| is the battery plugged in? | the JST PH 2.0 at the Feather |
| does USB-C bring it up? | if yes, the cell is flat or its protection has tripped: charge it for an hour |
| does it flash white at power-on? | if not, check the LED's orientation: long lead (anode) to VBAT |
| does the serial log show the banner? | `idf.py monitor`; if yes the MCU runs and the problem is the LED |

In normal operation the LED is **meant** to be dark. Press the button once:
it should show the air-quality colour for 3 seconds.

| LED | meaning |
|---|---|
| white flash at boot | alive |
| blue, slow blink | pairing mode (5 min): not commissioned yet, or you held the button 3–8 s |
| green / yellow / red / purple for 3 s after a press | air quality good / elevated / high / very high |
| white pulse after a press | still warming up (first minute after power-on) |
| yellow short blinks after a press | battery low |
| red blip every 10 s, unprompted | battery critical, measurement paused |
| red blip every 5 s, unprompted | all sensors failed: check the log |
| fast red for 3 s | factory reset in progress |
| white fast blink | a controller sent Identify |

## One colour is missing

A missing green or blue channel is almost always the LED fitted the wrong way
round, or a wrong resistor. Green and blue need about 3 V, so they only light
because the anode is on VBAT (3.3–4.2 V), not on 3V3.

## SGP40 / SCD41 never answer (`SGP40 init failed`, `SCD41 init failed`)

1. The sensors must be on the **sensor bus**: GPIO6 = SDA, GPIO7 = SCL. Those
   are the only pads the C6's LP_I2C can use. The Feather's own SDA/SCL
   (GPIO19/18) and its STEMMA QT port are the gauge bus, which is only
   powered for a few milliseconds every 5 minutes. A sensor plugged in there
   will never answer.
2. The 4.7 kΩ pull-ups R6/R7 must go to **+3V3_SENS**. Measure 3.3 V on SDA
   and SCL with the device running.
3. Is +3V3_SENS up? GPIO3 drives its load switch.

## The battery percentage never changes

The MAX17048 is read every 5 minutes with GPIO20 switched on briefly (EDR-11).
If the value is stuck across a day:

1. `battery` errors in the log mean the gauge bus did not come up. Check that
   nothing on the carrier holds GPIO19/18.
2. A value that reads but never moves may be the gauge's bus-low sleep mode.
   With GPIO20 off, both lines sit near 0 V. Set `gauge_interval_s` to 60 and
   see whether it starts tracking. Report it either way: this is an open
   verification item.

## PM2.5 reads 0.0, or never changes

1. Can you hear the fan? It runs for 40-60 s per measurement. Silence means
   the 5 V rail or the sensor.
2. Measure the 5 V rail during a measurement. If it is 3.4 V, the boost's
   enable is not being driven - check GPIO2 and the TPS22918.
3. `SPS30: ESP_ERR_TIMEOUT` in the log means the UART is not getting replies.
   Check TX and RX are not swapped, and that **SEL is left floating**. SEL
   pulled to ground selects I2C and the sensor will never answer on UART.
4. In ECO mode PM2.5 legitimately updates **once an hour**. A number that has
   not changed for an hour is correct behaviour.

## PM2.5 always reads low

This is the nasty one, because nothing looks wrong.

Hold the case up to a light with the bottom towards you. If you can see from
the inlet opening through to the outlet opening, the duct seal has failed and
the sensor is measuring its own exhaust. Check the foam strip is there and the
printed separating rib is solid.

Also worth checking: the device is not sitting in a draught above 1 m/s, and
nothing is blocking the bottom openings. It needs clearance underneath - use
the stand, or wall-mount it.

## VOC index sits at 100 and will not move

That is the algorithm working. 100 *means* "this room's 24 hour average". If
the room has been the same for a day, the index is 100.

It also takes time to settle: Sensirion specify under 60 s before VOC events
are reliably detected, and up to an hour before the sensor meets its full
specification. After a power cycle the whole algorithm starts again.

If the raw signal in the serial log does not move when you breathe on
it, that is a real fault - check the sensor rail is up.

## CO2 reads 400-ish and never moves

The SCD41 clamps at 400 ppm at the bottom. A room that reads exactly 400 all
day has either genuinely fresh air or a sensor that has been calibrated to
something wrong - most likely a forced recalibration performed indoors. See
`docs/CALIBRATION.md`.

## CO2 drifts over months

Expected in power-cycled single-shot mode without the sensor's own ASC. The
firmware substitutes its own, which needs the room to reach near-outdoor air
roughly weekly. If yours never does, either disable
`co2_self_calibration` and recalibrate manually now and then, or accept that
relative changes are meaningful and absolute values are not.

## Temperature reads 2-4 degC high

Self-heating inside a sealed box. Measure your device's actual offset against
a known-good thermometer and set it; the default is a datasheet default, not a
measurement of this enclosure.

## The device will not commission

1. Bluetooth on, on the phone.
2. Do you have an Apple Thread border router? HomePod mini, HomePod 2, or
   Apple TV 4K (2nd gen Wi-Fi + Ethernet or later). **Without one this cannot
   work** - the device has no Wi-Fi in the build.
3. Is the commissioning window open? It closes 15 minutes after boot. Hold
   the button 3-8 s to re-open it.
4. Already commissioned to another fabric? A 12 s press factory-resets it.
5. Move the device within a few metres of the border router for commissioning.
   You can move it afterwards.

## It shows up in Apple Home, then goes "No Response"

1. Range. Thread is 2.4 GHz and this is a low-power end device. Check the
   link quality in the serial log; below about -85 dBm it will be flaky.
2. The border router rebooted. The device re-attaches on its own, usually in
   seconds - give it a minute.
3. A 15 s gap is normal. The device polls every 15 s by design; the Home app
   will sometimes show it as briefly unresponsive.
4. Critical battery. Below 5 % the device stops measuring and only reports
   battery.

## Battery drains far faster than three months

In order of likelihood:

1. **It is not in ECO mode.** The serial log prints the mode on every change. If
   it says NORMAL, that is three weeks and it is working correctly. If it says
   ACTIVE, something is triggering event detection: lower `ev_sensitivity`.
2. **It never leaves ACTIVE.** A workshop with a permanently raised VOC level
   will re-trigger constantly. Reset the baseline once the room is at its
   normal state.
3. **Measure the idle current.** With a PPK II, the floor should be around
   100 uA. If it is about a milliamp or more, the Feather's WS2812B is
   powered, meaning GPIO20 is stuck high (EDR-11). Otherwise look for a load
   switch whose RTC hold is not being applied.
4. **The cell.** A tired or counterfeit LiPo will not deliver its rating.

## Events fire constantly / never fire

`ev_sensitivity` is a 1 to 5 dial; 3 is the default and reproduces the
built-in thresholds exactly.

| symptom | change |
|---|---|
| events all day in a busy workshop | lower to 2 or 1 |
| a print does not register | raise to 4 or 5 |
| events start and stop repeatedly | raise `ev_release_s` - the air is hovering around the threshold |

Nothing can trigger without a valid baseline, on purpose. A freshly reset
device will not detect anything for the first few samples.

## Factory reset

Hold the button for 12 to 20 seconds. Holding it longer than 20 s is ignored,
so a stuck button cannot wipe the device.

This erases the Matter fabric, the Thread credentials, the configuration, the
baseline and the history. It does not touch the firmware. The device reboots
into commissioning mode.

## Reading the log

```bash
cd firmware && idf.py -p /dev/tty.usbmodem* monitor
```

| line | meaning |
|---|---|
| `no stored config, using defaults` | first boot, or the config failed its CRC |
| `stored config failed its CRC` | NVS corruption; defaults were used rather than garbage |
| `baseline restored: PM2.5 8.1, VOC 100` | normal |
| `SPS30: ESP_ERR_TIMEOUT` | the particle sensor did not answer |
| `window 40s -> PM2.5 7.3 ug/m3 from 13 samples` | a normal measurement |
| `weekly fan cleaning` | normal, once a week |
| `event stored: peak PM2.5 22.4, VOC 176, 4130 s` | an event completed and was recorded |
| `forced recalibration to 420 ppm` | the CO2 self-calibration fired |
| `held 24000 ms, ignoring` | the button was held too long; nothing happened |
