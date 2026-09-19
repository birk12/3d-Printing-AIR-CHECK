# Apple Shortcuts recipes

Apple Home's own automation editor can trigger on one accessory's value
crossing a threshold. It cannot compare two accessories. Since comparing a
printer-side unit against a room-side unit is the whole reason to build two,
here is how to do it in Shortcuts.

All of these assume two commissioned devices named **3D Printer** and **Room**.

> These are written against Shortcuts as it behaves on iOS 18-26. Apple moves
> things. If an action is not where this says it is, look for it under
> Home > Get State or Home > Control.

---

## 1. Printer is dirtier than the room

The one that matters. A raised PM2.5 everywhere means a window is open; raised
only at the printer means the printer.

```
Automation: Home > "When PM2.5 of 3D Printer rises above 15"
  1  Get [PM2.5] of [3D Printer]        -> printerPM
  2  Get [PM2.5] of [Room]              -> roomPM
  3  Calculate: printerPM - roomPM      -> delta
  4  If delta is greater than 10
  5      Show notification:
         "Printer air: PM2.5 {printerPM}, room {roomPM}. Difference {delta}."
  6  End If
```

Step 1's trigger threshold matters: set it just above your normal room level
so the Shortcut is not woken constantly. 15 ug/m3 is a reasonable start.

---

## 2. Ventilate until it comes back down

```
Automation: Home > "When Air Quality of 3D Printer changes to Fair or worse"
  1  Turn [Ventilation Plug] on
  2  Repeat 24 times
  3      Wait 300 seconds
  4      Get [PM2.5] of [3D Printer]     -> pm
  5      Get [PM2.5] of [Room]           -> roomPM
  6      If pm is less than (roomPM + 5)
  7          Turn [Ventilation Plug] off
  8          Show notification "Printer air back to room level."
  9          Stop this shortcut
 10      End If
 11  End Repeat
 12  Turn [Ventilation Plug] off
 13  Show notification "Ventilation ran for 2 hours without recovering."
```

The counted loop matters. A `Repeat While` with no bound will run the fan
forever if the sensor fails, and that is the kind of automation people
discover from their electricity bill.

---

## 3. VOC rising, before particles do

In ECO mode the device only samples particles once an hour (CO2 every five
minutes), but VOC every ten seconds. VOC is the early warning.

```
Automation: Home > "When VOC of 3D Printer rises above 160"
  1  Get [VOC] of [3D Printer]          -> voc
  2  Show notification
     "VOC index {voc} at the printer. 100 is this room's 24 h average."
```

Remember what that number is: a VOC **Index**, not ppb. See
`docs/APPLE_HOME.md`.

---

## 4. Daily summary

```
Automation: Time of Day > 21:00, every day
  1  Get [PM2.5] of [3D Printer]        -> p1
  2  Get [PM2.5] of [Room]              -> p2
  3  Get [CO2] of [Room]                -> co2
  4  Get [Battery Level] of [3D Printer] -> b1
  5  Get [Battery Level] of [Room]      -> b2
  6  Show notification
     "Printer {p1} / Room {p2} ug/m3, CO2 {co2} ppm. Batteries {b1}% / {b2}%."
```

---

## 5. Battery, two stages

Apple Home can do this on its own, no Shortcut needed:

```
When   Battery Level of [3D Printer] drops below 25 %
Then   Send notification "AIR CHECK printer unit: battery low, plug in USB-C soon."
```

```
When   Battery Level of [3D Printer] drops below 10 %
Then   Send notification "AIR CHECK printer unit: plug in USB-C now."
       Turn on [a light]           (something you cannot ignore)
```

The level is coarse, read from the cell voltage: LiFePO4 is flat in the
middle, so trust it for "full" and "nearly empty", not in between. Below
3.20 V (about 10 %) the device warns by itself (yellow blinks on a press);
below 3.10 V (about 6 %) it stops measuring and only reports the battery.
The answer is always **plug in USB-C** - the device charges its cells
itself; nothing needs replacing. If Home shows that the battery needs
replacing, that is a latched charger fault: unplug and replug USB-C, then
`docs/TROUBLESHOOTING.md`.

On a USB-C charger the cells are the backup and are kept charged: the device
raises no low-battery state of its own, and the level stays up. Without cells
the level is null and nothing is reported (how Home shows that is not
verified).

---

## 6. Do not notify at night

```
Automation: Home > "When PM2.5 of 3D Printer rises above 25"
  1  Get current date
  2  Format date as "HH" -> hour
  3  If hour is between 7 and 22
  4      Show notification "Printer PM2.5 elevated."
  5  Otherwise
  6      Add to [a reminders list]: "Printer air was elevated overnight"
  7  End If
```

---

## Practical notes

* **The device answers in up to 15 seconds.** It is a sleepy Thread device
  with a 15 s poll. A Shortcut that reads five values may take a minute. Build
  that into any `Wait`.
* **Trigger on one device, read both.** Shortcuts cannot subscribe to two
  accessories at once; trigger on the one that moves first (the printer) and
  read the other inside the Shortcut.
* **A value can be unavailable.** During warm-up, or if a sensor has failed,
  the attribute is not published. Guard with `If [value] has any value`
  before comparing, or the Shortcut fails silently.
* **In ECO mode PM2.5 updates once an hour.** A Shortcut that polls it
  every five minutes will see the same number 12 times. Trigger on change
  instead.
