# Apple Home

## What you need

* an **Apple HomePod (2nd gen), HomePod mini, or Apple TV 4K (2nd gen Wi-Fi +
  Ethernet or newer)**. These are Apple's Thread border routers. Without one,
  a Thread device cannot reach Apple Home at all.
* an iPhone or iPad on a recent iOS. Humidity from a Matter air quality
  accessory in particular needs **iOS 26 or later** to be displayed.
* nothing else. No bridge, no hub app, no account with anybody.

## Adding a device

```
POWER ON  ->  WARMING UP screen  ->  Home app, "Add Accessory"
          ->  scan the QR code on the back of the case
          ->  the device joins your Thread network
          ->  name it, put it in a room
```

If the device was already commissioned and you want to re-open the
commissioning window, hold the button for 3 to 8 seconds. The screen switches
to the pairing screen and shows the manual code.

## What Apple Home shows

| what the device measures | Matter cluster | shown in Apple Home? |
|---|---|---|
| overall air quality | Air Quality `0x005B` | **yes** - the accessory tile reads Excellent / Good / Fair / Inferior / Poor |
| PM2.5 | PM2.5 Concentration `0x042A` | **yes**, in ug/m3 |
| PM10 | PM10 Concentration `0x042D` | **yes**, in ug/m3 |
| PM1.0 | PM1 Concentration `0x042C` | **no.** HomeKit has no PM1 characteristic. The value is published and any full Matter controller can read it; Apple simply does not surface it. It is on screen 2 of the device. |
| CO2 | CO2 Concentration `0x040D` | **yes**, in ppm |
| VOC | TVOC Concentration `0x042E` | **yes, but read the caveat below** |
| temperature | Temperature Measurement | **yes**, as a separate sensor in the same accessory |
| humidity | Relative Humidity Measurement | **yes** on iOS 26 and later |
| battery | Power Source (battery feature) | **yes** - level, and a low-battery warning |
| charging | Power Source status | **yes**, as the accessory's battery state |

### The VOC caveat

The SGP40 produces a **VOC Index from 1 to 500**, where 100 means "the average
of this room over the last 24 hours". It is a relative indicator of how the
air has changed. It is **not** a concentration, and it cannot be converted
into one - the sensor does not know which chemicals it is smelling.

Matter's TVOC cluster wants a concentration, so the firmware publishes the
index as a number and declares the unit as ppb. **The number Apple Home labels
"VOC" on this device is a VOC Index, not parts per billion.** Read it as:

| VOC Index | meaning |
|---|---|
| under 100 | cleaner than this room's own 24 h average |
| 100 | the running average |
| 150 - 250 | noticeably elevated, something is off-gassing |
| over 250 | strongly elevated |

If you would rather have no number than a mislabelled one, set
`voc_publish_index_as_ppb` to false. Apple Home then shows no VOC value, and
VOC still influences the overall Air Quality tile and still triggers the
device's own event detection.

## Air quality mapping

The device's four states map onto Matter's `AirQualityEnum`, which Apple Home
renders with its own words:

| device | Matter enum | Apple Home |
|---|---|---|
| GOOD | 1 Good | Good |
| ELEVATED | 3 Moderate | Fair |
| HIGH | 4 Poor | Inferior |
| VERY HIGH | 5 VeryPoor | Poor |

The device's own screen always says *why*: "HIGH - PM2.5 + VOC". Apple Home
does not carry that reason, which is one of the reasons the device has a
screen.

## Automations that work

These are plain Home app automations, no Shortcuts needed:

```
When  Air Quality of "3D Printer" changes to Fair or worse
Then  send a notification
```

```
When  PM2.5 of "3D Printer" rises above 25
Then  turn on the ventilation plug
```

```
When  the battery of "Room" drops below 20 %
Then  send a notification
```

Apple Home's numeric triggers work on the PM2.5, PM10, CO2 and VOC values, so
anything expressible as "this number crossed that threshold" is a two minute
job in the Home app.

## Automations that need Shortcuts

Anything that compares **two accessories** - which is the whole point of
building two AIR CHECKs - cannot be expressed in the Home app's automation
editor. `docs/SHORTCUTS.md` has working recipes for:

* printer PM2.5 more than 10 ug/m3 above room PM2.5
* keep ventilation running until the printer unit comes back down
* a daily summary of both units

## Known limitations

1. **PM1 is invisible in Apple Home.** No HomeKit characteristic exists. Use
   the device screen, or a full Matter controller.
2. **The VOC number is an index, not a concentration.** See above.
3. **Reaction time is up to 15 seconds.** The device is a Matter ICD with a
   15 s slow poll - the longest a Short Idle Time ICD is allowed. A command or
   a subscription update waits for the next poll. This is the direct price of
   the battery life.
4. **PM2.5 in ECO mode updates every four hours.** Apple Home will show the
   same number for hours at a time and that is correct behaviour, not a stuck
   sensor. Switch to NORMAL if you want a fresher number and can charge the
   device every three weeks.
5. **The air-quality *reason* is not exported.** Matter has nowhere to put it.
6. **Everything in this table should be verified against your own setup.**
   Apple changes what Home surfaces between iOS releases. Nothing here has
   been tested against a real HomePod by the authors - see
   `docs/TESTING.md` for exactly what has and has not been verified.

## Two devices

Build two, commission them one after the other, and name them in the Home app:

```
AIR CHECK  ->  "3D Printer"   Room: Workshop
AIR CHECK  ->  "Room"         Room: Workshop
```

They are two completely independent Matter accessories. There is no pairing
between them, no master, no synchronisation, and nothing in the firmware knows
that a second unit exists. You can build a third and a fourth.

The one thing to watch: both ship with the same default name on the display.
Rename them in the config (or accept that the Home app name is the one that
matters) so the diagnostics screen tells them apart - it shows a serial number
derived from the MAC, which is unique per unit.
