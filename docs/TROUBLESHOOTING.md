# Troubleshooting

Work down the list. Almost everything is one of the first five.

## The LED does nothing

| check | |
|---|---|
| is the battery plugged in? | the cell into J2 on the carrier, and the J3 pigtail into the FireBeetle |
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
| blue / cyan / red **while you hold the button** | what letting go would do: pairing / CO2 calibration / factory reset |
| cyan, slow blink for 3 min | fresh-air CO2 calibration running |
| green 2 s, or fast red 2 s, after that | calibration done, or failed |

## One colour is missing

A missing green or blue channel is almost always the LED fitted the wrong way
round, or a wrong resistor. Green and blue need about 3 V, so they only light
because the anode is on VBAT (3.3–4.2 V), not on 3V3.

## `SGP40 init failed`

1. The breakout must be on the **VOC bus**: GPIO6 = SDA, GPIO7 = SCL. Those
   are the only pads the C6's LP_I2C can use.
2. The 4.7 kΩ pull-ups R6/R7 must go to **+3V3_SENS**, the switched rail.
3. Is +3V3_SENS switching? GPIO3 drives SW2. The rail is only up for about
   0.25 s every 10 s, so a multimeter will read almost nothing; a scope or the
   PPK2 will show the pulses. Check C5 (1 nF) is fitted on SW2's CT pin.

## `SEN63C not responding`

1. The SEN63C is on the FireBeetle's **SDA/SCL** header pins (GPIO19/20),
   through J1. Check the GH cable is pin 1 to pin 1 - a reversed cable puts
   VDD on SCL.
2. R11/R12 must go to **+3V3_SEN6X**.
3. Is +3V3_SEN6X there during a window? It should read 3.2-3.4 V for the 40 s
   the fan runs. If it sags or the chip resets when it switches on, check C2
   (4.7 nF) on SW1's CT pin and C1 (22 µF).

## The battery percentage looks wrong

There is no fuel gauge since v1.2. The percentage comes from the cell voltage
(`ac_core/ac_battery.c`) and is honest to about ±10 % in the middle of the
discharge, better near full and near empty.

1. Compare the logged voltage (`diag` on the console) with a multimeter at the
   cell. More than 30 mV apart: the ADC has no eFuse calibration (the boot log
   says so) or the divider on the FireBeetle is off.
2. The value only falls on battery, on purpose - it does not climb back when
   the cell warms up. It follows the voltage up again only on USB or when a
   charged cell is fitted.
3. On USB it reads high while charging. That is the charge voltage, not the
   state of charge.

## PM2.5 reads 0.0, or never changes

1. Can you hear the fan? It runs for about 40 s an hour in ECO, 60 s in the
   other modes. Silence at the top of the hour means the rail or the sensor
   (see `SEN63C not responding`).
2. `SEN63C: ESP_ERR_INVALID_RESPONSE` in the log means the module reported a
   fan or laser error. The status register is printed with it.
3. In ECO mode PM2.5 legitimately updates **once an hour**. A number that has
   not changed for an hour is correct behaviour.

## PM2.5 always reads low

This is the nasty one, because nothing looks wrong.

The SEN63C must draw its air through the slots in the left wall, and only from
there. If the foam frames around its inlet and outlet windows are missing or
leaking, it breathes air from inside the case and blows its own exhaust back
in. Open the case and check both gaskets are there and pressed flat.

Also worth checking: the device is not sitting in a draught above 1 m/s (a
printer's part-cooling or exhaust fan counts), the left side is not against a
wall, and the slots are not blocked.

## VOC index sits at 100 and will not move

That is the algorithm working. 100 *means* "this room's 24 hour average". If
the room has been the same for a day, the index is 100.

It also takes time to settle: Sensirion specify under 60 s before VOC events
are reliably detected, and up to an hour before the sensor meets its full
specification. After a power cycle the whole algorithm starts again.

If the raw signal in the serial log does not move when you breathe on
it, that is a real fault - check the sensor rail is up.

## CO2 reads -1 / is missing

The SEN63C's CO2 output is "unknown" for the first 22-24 s of every
measurement. If the log shows CO2 as -1 after a window, the window was too
short or the module's CO2 channel reported an error (status register in the
same log line). The firmware refuses windows under 30 s for this reason.

## CO2 reads 400-ish and never moves, or drifts over weeks

A room that reads exactly 400 all day has either genuinely fresh air or a
sensor calibrated to something wrong - most likely a fresh-air calibration
done indoors. A reading that creeps up over weeks in a room you air daily is
drift that ASC has not caught; this device runs the SEN63C only 40 s an hour,
and whether its ASC converges on that is still being tested (B16). Either way:
take it outdoors and hold the button 8-12 s (`docs/CALIBRATION.md`).

## Temperature reads a degree or two off

The SEN63C compensates its own heating, but not the case's. If it is off
against a thermometer you trust, note the offset - there is no offset setting
yet (`docs/CALIBRATION.md`).

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
   70 µA with both sensor rails off. The usual suspects above that: a sensor
   rail that stays on (the SGP40 breakout's LED lights up - it should only
   flicker every 10 s), a pin back-feeding an unpowered sensor, or the
   FireBeetle's green LED on GPIO15 being driven.
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
| `SEN63C: ESP_ERR_TIMEOUT` | the particle/CO2 module did not answer |
| `window 40s: PM2.5 7.3 ug/m3 (10 samples), CO2 512 ppm, 22.8 C, 45 %RH` | a normal measurement |
| `fan speed warning (status 0x...)` | the SEN63C's fan is off its nominal speed; watch whether it turns into an error |
| `event stored: peak PM2.5 22.4, VOC 176, 4130 s` | an event completed and was recorded |
| `fresh-air CO2 calibration to 425 ppm: 3 min run first` | a button or console calibration started |
| `CO2 recalibrated: correction -37 ppm` | and finished; the correction is what the sensor was off by |
| `held 24000 ms, ignoring` | the button was held too long; nothing happened |
