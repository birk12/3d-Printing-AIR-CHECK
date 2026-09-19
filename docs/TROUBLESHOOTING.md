# Troubleshooting

Work down the list. Almost everything is one of the first five.

## The LED does nothing

| check | |
|---|---|
| are the cells in? | four AER18650m2A2, matching the + marks in the holders, one type and batch |
| are the cells above the cut-off? | below 3.0 V the #6091 switches LOAD off (BUVLO) to protect the cells; it comes back with USB-C in J2, or once the cells are at 3.15 V again. The BMS cuts off at 2.1 V: USB-C in J2 |
| does USB-C in J2 bring it up? | if yes, everything from the #6091 on is fine and the problem is on the cell side: flat cells, an open fuse, the BMS. If not, measure #6091 LOAD (4.41–4.59 V with USB-C), then PS1's VOUT |
| is the power path complete? | cells → one PICO fuse each → BMS B+; BMS P+ → #6091 BATT+, BMS P− → BATT− (system GND); #6091 LOAD+ → PS1 VIN; PS1 VOUT → LM66200 VIN1 → pigtail + into the FireBeetle's battery socket. LM66200 VIN2 and ON to GND. Check the pigtail polarity against the "+" on the FireBeetle |
| is the regulator set? | 3.90 V at PS1's VOUT (ASSEMBLY step 4.3). 0 V: no LOAD (cells below the cut-off and no USB-C) or a lead is open |
| does the FireBeetle's USB-C bring it up? | if yes, the MCU is fine; if neither the cells nor J2 do, suspect the LM66200 (ON to GND?) and the pigtail |
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
| yellow short blinks after a press | battery low (about 10 %, 3.20 V): **plug in USB-C** |
| red blinks for 3 s after a press | all sensors failed: check the log |
| red blip every 10 s, unprompted | battery critical (about 6 %, 3.10 V), measurement paused: plug in USB-C now |
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
the anode is on VSYS (3.9 V from the regulator; 4.2 V while a computer is on
the FireBeetle's USB-C), not on 3V3.

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

## Low or empty battery

The yellow low-battery blink (3.20 V, about 10 %) means **plug in USB-C**;
the red blip every 10 s (3.10 V, about 6 %) means the device has stopped
measuring and only reports the battery. Nothing needs replacing - the device
charges its cells itself.

At 3.0 V the #6091 switches the device off (BUVLO); USB-C in J2 or cells back
at 3.15 V bring it up again. The BMS is the last line at 2.1 V. For storage
and transport see [`NUTZUNG.md`](NUTZUNG.md) §6.

On USB-C there is no low or critical battery state: the device runs from the
charger. Matter still reports `BatChargeLevel` and `BatVoltage` for the cells.
USB-C without cells raises no battery alarm at all (`Status` 3, `BatPresent`
false).

## Which power source is it using?

The log prints a line on every change of source, charge state or fault:

| log | meaning | Matter `Status`: battery (endpoint 0) / USB-C endpoint |
|---|---|---|
| `power: cells, cells 3.30 V (55 %), not charging, VSYS 3.90 V` | running on the cells | 1 Active / 3 Unavailable |
| `power: USB-C, cells ..., charging, VSYS 3.90 V` | USB-C in J2, the cells charge | 2 Standby / 1 Active |
| `power: USB-C, cells ..., full, charge paused, VSYS 3.90 V` | USB-C, cells full, charging paused (see below) | 2 Standby / 1 Active |
| `power: USB-C, no cells, ...` | USB-C, no cells (cell voltage under 1.0 V) | 3 Unavailable / 1 Active |

USB-C in J2 is recognised from the PWR-K node on GPIO4 (above 0.6 V). A
computer on the FireBeetle's USB-C counts as external power for the
measurement engine (CONTINUOUS, no battery alarms) and for the USB-C
endpoint, but the log still says `cells`: that port never charges them. A
charger in J2 that does not show up: measure the node (TESTING T-L1) - 0 V
means R20 or its wire to DCIN+ is open.

## Charging does not start, or stops with a fault

| log | cause | what to do |
|---|---|---|
| `fault (temperature/OVP)` | the NTC sees the cells outside 0–60 °C (a cold or hot room, sun, a radiator), or the NTC lead is open (reads as far too cold), or an input overvoltage | room temperature, 0–45 °C; check the NTC: 9–11 kΩ at 25 °C, bead on its cell (TESTING PS-2.5). The device keeps running from USB-C meanwhile |
| `FAULT (timer?)` after more than 5.5 h of charging | the BQ25185's 6 h safety timer. From flat, 1S4P needs 8–9 h: the firmware restarts charging once per USB session (`safety timer ran out before full: charging restarted once`) | nothing, the first time. A second timer fault in the same session stays, and Matter shows `BatReplacementNeeded`: unplug USB-C and plug it in again. If it recurs, measure the cells - one may be faulty; [`NUTZUNG.md`](NUTZUNG.md) §3/§4 |
| `charging` for many hours | normal from flat: 8–9 h, the device's own draw shares the 1.1 A input limit | – |
| charging much slower than that | the "1 Amp" jumper is not bridged: the factory default is 500 mA (TESTING PS-2.8) | bridge it |
| `full`, `charge paused` on permanent USB-C | the charge pause: the firmware holds CE high after full and releases it on unplug, below 3.30 V or after 30 days | nothing - that is intended |

Never fit cells to a #6091 that reads 4.1–4.25 V at BATT without cells: its
VS jumper is still on the factory 4.2 V (TESTING PS-1.2/1.3).

A hot, swollen, dented or leaking cell, a sharp smell, or a damaged
compartment: unplug, do not charge again, and follow
[`NUTZUNG.md`](NUTZUNG.md) §4.

## The battery percentage looks wrong

There is no fuel gauge. The percentage is coarse, read from the cell
voltage (`pwr_std`), and LiFePO4 is flat between about 3.26 and 3.33 V: trust
it for "full" and "nearly empty", not in between. While the cells charge the
voltage reads high. The low and critical alarms come from the voltage
itself (3.20 / 3.10 V), not from the percentage.

1. Compare `LFP cells X.XX V` in `diag` with a multimeter across BMS P+ / P−.
   The 470k/470k divider doubles any ADC error; if the boot log says `no
   ADC calibration in eFuse; battery level unavailable`, there is no reading
   at all.
2. On USB-C, once the cells are full and during the charge pause, it shows
   100 %.

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
4. Critical battery. Below 3.10 V (about 6 %) the device stops measuring and
   only reports the battery. At 3.0 V the charger switches it off altogether;
   USB-C brings it back.

## Battery drains far faster than expected

ECO on the cells is modelled at 2.9 months with margin
(`docs/BATTERY_LIFE.md`). If that is not enough, run it from a USB-C charger
in J2 permanently - that is allowed, and the cells stay topped up. In order
of likelihood:

1. **It is not in ECO mode.** The serial log prints the mode on every change. If
   it says NORMAL, that is about four weeks on the cells and it is working
   correctly. If it says ACTIVE, something is triggering event detection:
   lower `ev_sensitivity`.
2. **It never leaves ACTIVE.** A workshop with a permanently raised VOC level
   will re-trigger constantly. Reset the baseline once the room is at its
   normal state.
3. **Measure the idle current** (TESTING T-E0, T-E1): the model says about
   2.6 mW at the cells with the SEN62 off, of which the cells' self-discharge
   and the regulator's quiescent current are the biggest part. The PPK2 goes
   in the 3.9 V line between PS1 and the LM66200, a µA meter in the BMS P+
   lead. The usual suspects above that: the #2810's slide switch in ON (the
   SEN62 then never switches off, and the #2810's red LED stays lit), the
   SparkFun PWR jumper not cut, the FireBeetle's green LED on GPIO15 being
   driven, or a pin back-feeding an unpowered sensor.
4. **The cells.** Cells that lose capacity are replaced all four at once, one
   type and batch, charged together first (`ASSEMBLY.md`, "Changing cells").

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
| `power: LFP 1S4P module; site 520 m = 952 hPa` | the power module, and the pressure the CO2 is corrected to |
| `power: USB-C, cells 3.41 V (100 %), full, charge paused, VSYS 3.90 V` | source, cell voltage, charge state and any fault changed ("Which power source is it using?") |
| `safety timer ran out before full: charging restarted once` | the charger's 6 h timer ran out; charging restarted, once per USB session |
| `no ADC calibration in eFuse; battery level unavailable` | the cell voltage cannot be read reliably; no battery level is reported |
| `SEN62: ESP_ERR_TIMEOUT` | the particle module did not answer |
| `window 60s: PM2.5 7.3 ug/m3 averaged over 30 s` | a normal particle window |
| `fan speed warning (status 0x...)` | the SEN62's fan is off its nominal speed; watch whether it turns into an error |
| `CO2 512 ppm (952 hPa)` | a normal Sunrise measurement, with the pressure it was given |
| `event stored: peak PM2.5 22.4, VOC 176, 4130 s` | an event completed and was recorded |
| `fresh-air CO2 calibration to 425 ppm: 3 min settling first` | a button or console calibration started |
| `CO2 calibrated to 425 ppm (it read 447)` | and finished; the difference is what the sensor was off by |
| `held 24000 ms, ignoring` | the button was held too long; nothing happened |
