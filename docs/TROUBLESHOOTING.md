# Troubleshooting

Work down the list. Almost everything is one of the first five.

## The LED does nothing

| check | |
|---|---|
| are the cells in? | six AA, matching the + marks in the holder, all of one type and age |
| is the power path complete? | holder red lead → PTC fuse → S9V11E2A VIN; VOUT → LM66200 VIN1 → pigtail + into the FireBeetle's battery socket. Check the pigtail polarity against the "+" on the FireBeetle |
| is the regulator set? | 4.00 V at its VOUT (ASSEMBLY step 4). 0 V: the fuse has tripped or a lead is open |
| does USB-C bring it up? | if yes, the MCU is fine and the problem is in the pack path above: flat cells, the fuse, the regulator or the ideal diode |
| does it flash white at power-on? | if not, check the LED's orientation: long lead (common anode) to VSYS |
| does the serial log show the banner? | `idf.py monitor`; if yes the MCU runs and the problem is the LED |

In normal operation the LED is **meant** to be dark. Press the button once:
it should show the air-quality colour for 3 seconds. The red LED flickering
during boot is normal: it sits on GPIO16, which the ROM boot log toggles.

| LED | meaning |
|---|---|
| white flash at boot | alive |
| blue, slow blink | pairing mode (5 min): not commissioned yet, or you held the button 3–8 s |
| green / yellow / red / purple for 3 s after a press | air quality good / elevated / high / very high |
| white pulse after a press | still warming up (first minute after power-on) |
| yellow short blinks after a press | battery low: change the cells |
| red blinks for 3 s after a press | all sensors failed: check the log |
| red blip every 10 s, unprompted | battery critical, measurement paused |
| red blip every 5 s, unprompted | all sensors failed: check the log |
| fast red for 3 s | factory reset in progress |
| white fast blink | a controller sent Identify |
| blue / cyan / red **while you hold the button** | what letting go would do: pairing / CO2 calibration / factory reset |
| cyan, slow blink (up to 4 min) | fresh-air CO2 calibration running |
| green 2 s, or fast red 2 s, after that | calibration done, or failed |

## One colour is missing

A missing green or blue channel is almost always the LED fitted the wrong way
round, or a wrong resistor (R8 1 k on red to GPIO16, R9/R10 330 Ω on green and
blue to GPIO22/23). Green and blue need about 3 V, so they only light because
the anode is on VSYS (about 4.0 V), not on 3V3.

## `SGP40 init failed` / `SHT40 not responding`

1. Both boards are on the **always-on bus**: GPIO6 = SDA, GPIO7 = SCL. Those
   are the only pads the C6's LP_I2C can use. Qwiic colours: blue SDA, yellow
   SCL; Grove: white SDA, yellow SCL.
2. Both run from the FireBeetle's 3V3, permanently. There are no separate
   pull-up resistors: the bus uses the SparkFun board's 4.7 kΩ (its I2C
   jumper must stay **closed**) and the Grove board's own.
3. `SGP40 init failed` also means the chip's self test did not return 0xD400.
   A board that fails it on a correctly wired bus is faulty.
4. The SparkFun PWR jumper should be cut. That does not stop the sensor, but
   its LED costs more than the sensor.

## `SEN62 not responding`

1. The SEN62 is on the FireBeetle's **SDA/SCL** header pins (GPIO19/20).
   Check the GH cable: pins 1 and 6 VDD, 2 and 5 GND, 3 SDA, 4 SCL. A reversed
   cable puts VDD on SCL.
2. R3/R4 (4.7 kΩ) must go to the #2810's **VOUT**, the switched supply.
3. The #2810's slide switch must be in **OFF**; only then does GPIO2 on its
   ON pin have control.
4. Is +3V3_SEN there during a window? It should read 3.2-3.4 V for the 60 s
   the fan runs. If the FireBeetle resets or the other sensors fail when it
   switches on, that is the switch-on step the ERC warns about (TESTING T-P3):
   the #2810 has no soft start.

## `Sunrise not responding`

The boot log shows `sunrise: not answering: ...` first.

1. COMSEL (pin 6) to GND - it selects I2C.
2. SDA pin 4 to **GPIO17**, SCL pin 5 to **GPIO21**, VDDIO pin 3 to
   **GPIO18**. R5/R6 (10 kΩ) across pins 3-4 and 3-5.
3. EN pin 9 to **GPIO14**, with R7 (100 kΩ) to GND at the FireBeetle.
4. VBB pin 2 on VSYS (the pigtail + lead), not on 3V3.

`EEPROM configuration updated (n register(s)), sensor reset` on the very
first boot is normal: the firmware writes single-measurement mode, 32
samples and its ABC settings once, and only what differs.

## The battery percentage looks wrong

There is no fuel gauge. The percentage comes from the pack voltage, per cell
chemistry, and an energy counter (`ac_core/ac_battery.c`); the lower of the
two wins. It is good enough for "a quarter left" and "nearly empty".

1. **Is `cell_type` set?** The default is alkaline. L91 lithium and NiMH have
   different curves: `set cell_type lithium` (or `nimh`).
2. Compare the pack voltage in `diag` with a multimeter across the pack. The
   1M/220k divider multiplies any ADC error by 5.5; if the boot log says `no
   ADC calibration in eFuse; battery level unavailable`, there is no
   percentage at all.
3. L91 cells stay flat for most of their life. There the energy counter
   carries the percentage, not the voltage.
4. The value only falls, on purpose. Fresh cells are recognised by the jump
   in voltage (0.08 V per cell) and restart the counter; half-used cells put
   back in may not be.

## PM2.5 reads 0.0, or never changes

1. Can you hear the fan? It runs for 60 s an hour in ECO; the first 30 s are
   discarded while it settles. An hour of silence means the switch or the
   sensor (see `SEN62 not responding`).
2. `SEN62: ESP_ERR_INVALID_RESPONSE` in the log means the module reported a
   fan or laser error. The status register is printed with it.
3. In ECO mode PM2.5 legitimately updates **once an hour**. A number that has
   not changed for an hour is correct behaviour.

## PM2.5 always reads low

This is the nasty one, because nothing looks wrong.

The SEN62 must draw its air through the slots in the left wall, and only from
there. If the EPDM frames around its inlet and outlet windows are missing or
leaking, it breathes air from inside the case and blows its own exhaust back
in. Open the case and check both frames are there and pressed flat, the port
face against the wall.

Also worth checking: the device is not sitting in a draught above 1 m/s (a
printer's part-cooling or exhaust fan counts), the left side is not against a
wall, and the slots are not blocked.

## VOC index sits at 100 and will not move

That is the algorithm working. 100 *means* "this room's 24 hour average". If
the room has been the same for a day, the index is 100.

It also takes time to settle: Sensirion specify under 60 s before VOC events
are reliably detected, and up to an hour before the sensor meets its full
specification. After a power cycle the whole algorithm starts again.

If a marker pen opened at the gas-bay vents does not move it within about a
minute (TESTING T-G1), that is a real fault: check the vents in the right
side wall and the front face are open, and that the bay is closed off from
the rest of the case by its walls and the lid ribs.

## VOC index drifts for days after assembly

Something in the gas bay is giving off volatiles and the SGP40 is learning it
as its baseline. The usual causes: printed parts not baked out (24 h at
50–60 °C, `manufacturing/print-settings.md`), or tape, glue, foam or hot glue
inside the bay. Nothing in the bay may be stuck down.

## CO2 is missing

`Sunrise: ESP_ERR_TIMEOUT` means the sensor did not finish a measurement in
the time the firmware waits for 32 samples. `Sunrise: ESP_ERR_INVALID_RESPONSE`
means it reported a fatal, algorithm, self-diagnostics or memory error; the
line before it prints the error status. After three failures in a row the
channel is marked down and not tried again until the next boot (`diag` shows
the health).

## CO2 reads 400-ish and never moves, or drifts over weeks

A room that reads exactly 400 all day has either genuinely fresh air or a
sensor calibrated to something wrong - most likely a fresh-air calibration
done indoors. A reading that is off by a steady few percent may be the
pressure: set `altitude_m` (the default is 0 m, and NDIR reads 1.6 % per kPa).
Weather alone moves it by about ±3 %.

The Sunrise's self-calibration (ABC, 180 h period, 425 ppm target) needs fresh
air about once a week. In a room that is never aired it drifts; set
`co2_self_calibration false` there. Either way: take it outdoors and hold the
button 8-12 s (`docs/CALIBRATION.md`), and check against a reference over a
week (TESTING T-S7).

## Temperature reads a degree or two off

The SHT40 sits low in the gas bay, next to the vents and away from the ESP32
and the Sunrise's lamp, but it is still inside a case. If it is off against a
thermometer you trust, note the offset - there is no offset setting yet
(`docs/CALIBRATION.md`).

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

## Battery drains far faster than expected

ECO on L91 cells is modelled at 3.7 months, NiMH 2.2, alkaline 2.1
(`docs/BATTERY_LIFE.md`). In order of likelihood:

1. **It is not in ECO mode.** The serial log prints the mode on every change. If
   it says NORMAL, that is about five weeks on L91 and it is working correctly.
   If it says ACTIVE, something is triggering event detection: lower
   `ev_sensitivity`.
2. **It never leaves ACTIVE.** A workshop with a permanently raised VOC level
   will re-trigger constantly. Reset the baseline once the room is at its
   normal state.
3. **Measure the idle current** (TESTING T-E1): PPK2 in the pack lead, the
   model says about 0.33 mA from the pack with the SEN62 off. The usual
   suspects above that: the #2810's slide switch in ON (the SEN62 then never
   switches off, and the #2810's red LED stays lit), the SparkFun PWR jumper
   not cut, the FireBeetle's green LED on GPIO15 being driven, or a pin
   back-feeding an unpowered sensor.
4. **The cells.** Mixed ages or chemistries. Change all six at once, one
   type, and set `cell_type`.

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
| `no usable stored config, using defaults` | first boot, or the config failed its CRC |
| `stored config failed its CRC, using defaults` | NVS corruption; defaults were used rather than garbage |
| `baseline restored: PM2.5 8.1, VOC 100` | normal |
| `power: 6 x AA lithium, 1234 mWh used; site 520 m = 952 hPa` | the cell type, the energy counter and the pressure the CO2 is corrected to |
| `SEN62: ESP_ERR_TIMEOUT` | the particle module did not answer |
| `window 60s: PM2.5 7.3 ug/m3 averaged over 30 s` | a normal particle window |
| `fan speed warning (status 0x...)` | the SEN62's fan is off its nominal speed; watch whether it turns into an error |
| `CO2 512 ppm (952 hPa)` | a normal Sunrise measurement, with the pressure it was given |
| `event stored: peak PM2.5 22.4, VOC 176, 4130 s` | an event completed and was recorded |
| `fresh-air CO2 calibration to 425 ppm: 3 min settling first` | a button or console calibration started |
| `CO2 calibrated to 425 ppm (it read 447)` | and finished; the difference is what the sensor was off by |
| `held 24000 ms, ignoring` | the button was held too long; nothing happened |
