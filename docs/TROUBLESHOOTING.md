# Troubleshooting

Work down the list. Almost everything is one of the first five.

## Nothing on the screen

| check | |
|---|---|
| is the battery plugged in? | the JST PH 2.0 at the Feather |
| does USB-C bring it up? | if yes, the cell is flat or its protection has tripped - charge it for an hour |
| is the e-paper ribbon seated? | the connector latch has to be flipped closed |
| does the serial log say anything? | `idf.py monitor`; if the banner appears the MCU is fine and the panel is not |

An e-paper panel holds its last image with no power at all. A **blank white**
screen means the panel was cleared or never written. A screen still showing
old values means the device stopped refreshing - check the log, not the panel.

## `e-paper init failed` in the log

The `BUSY` line is stuck high. Usually the ribbon, occasionally the 100 k
pull-down not being fitted. The firmware gives up after 2 s rather than
hanging, so the rest of the device keeps working with a stale screen.

## PM2.5 reads 0.0, or never changes

1. Can you hear the fan? It runs for 40-60 s per measurement. Silence means
   the 5 V rail or the sensor.
2. Measure the 5 V rail during a measurement. If it is 3.4 V, the boost's
   enable is not being driven - check GPIO2 and the TPS22918.
3. `SPS30: ESP_ERR_TIMEOUT` in the log means the UART is not getting replies.
   Check TX and RX are not swapped, and that **SEL is left floating**. SEL
   pulled to ground selects I2C and the sensor will never answer on UART.
4. In ECO mode PM2.5 legitimately updates **every four hours**. A number that
   has not changed since this morning is correct behaviour.

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

If the raw signal on the diagnostics screen does not move when you breathe on
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
   diagnostics screen's link quality; below about -85 dBm it will be flaky.
2. The border router rebooted. The device re-attaches on its own, usually in
   seconds - give it a minute.
3. A 15 s gap is normal. The device polls every 15 s by design; the Home app
   will sometimes show it as briefly unresponsive.
4. Critical battery. Below 5 % the device stops measuring and only reports
   battery.

## Battery drains far faster than six months

In order of likelihood:

1. **It is not in ECO mode.** The status line, top right, says which mode. If
   it says NORMAL, that is three weeks and it is working correctly. If it says
   ACTIVE, something is triggering event detection - lower `ev_sensitivity`.
2. **It never leaves ACTIVE.** A workshop with a permanently raised VOC level
   will re-trigger constantly. Reset the baseline once the room is at its
   normal state.
3. **Measure the idle current.** With a PPK II, the floor should be around
   120 uA. If it is milliamps, something is not being switched off - the most
   likely culprits are the STEMMA QT rail (GPIO20 should be low) or a load
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
