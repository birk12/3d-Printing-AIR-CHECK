# Matter

Built against **esp-matter release/v1.6** on ESP-IDF v5.5.5.

The code targets esp-matter's *legacy* data-model implementation, which is the
default. esp-matter also ships a generated data model behind
`CONFIG_ESP_MATTER_ENABLE_GENERATED_DATA_MODEL`, but that option is marked
experimental upstream and the cluster namespaces differ
(`pm25_concentration_measurement` against `pm2_5_concentration_measurement`,
and the concentration clusters share one config type in the legacy API). If
you turn it on, `ac_matter.cpp` needs the generated names.

## Endpoints

| ep | device type | id | clusters (server) |
|---|---|---|---|
| 0 | Root Node | `0x0016` | Descriptor, Basic Information (**NodeLabel = configured name**), General Commissioning, Network Commissioning, ICD Management, **Power Source** |
| 1 | Air Quality Sensor | `0x002C` | Descriptor, Identify, **Air Quality** `0x005B`, **PM2.5** `0x042A`, **PM10** `0x042D`, **PM1** `0x042C`, **CO2** `0x040D`, **TVOC** `0x042E` |
| 2 | Temperature Sensor | `0x0302` | Descriptor, Identify, Temperature Measurement `0x0402` |
| 3 | Humidity Sensor | `0x0307` | Descriptor, Identify, Relative Humidity Measurement `0x0405` |
| next (the boot log prints it as `usb-c=N`) | Power Source | `0x0011` | Descriptor, **Power Source** "USB-C" (Wired) |

Every one of these is a standard device type with standard clusters. There are
no manufacturer-specific clusters and no vendor extensions anywhere in the
model. That is deliberate: a proprietary cluster would work with nothing.

## Cluster detail

### Air Quality `0x005B`

Features enabled: `FAIR`, `MOD` (Moderate), `VPOOR` (VeryPoor). `XPOOR`
(ExtremelyPoor) is deliberately left off, because none of the device's
thresholds would ever produce it and advertising a value you never send is
misleading.

| device state | `AirQuality` attribute |
|---|---|
| GOOD | 1 Good |
| ELEVATED | 3 Moderate |
| HIGH | 4 Poor |
| VERY HIGH | 5 VeryPoor |
| nothing measured yet | 0 Unknown |

### Concentration measurement clusters

All five have the `NumericMeasurement` (`MEA`) feature and `MeasurementMedium`
= Air. PM2.5, PM10, CO2 and TVOC also have `PeakMeasurement` (`PEA`) and
`AverageMeasurement` (`AVG`) with 24 h windows. They are computed from the
device's own 5-minute history, so a dashboard that sleeps can still show
"worst today" (EDR-12).

| cluster | unit attribute | range advertised | source |
|---|---|---|---|
| PM2.5 `0x042A` | 4 = UGM3 | 0 .. 1000 | SEN62 |
| PM10 `0x042D` | 4 = UGM3 | 0 .. 1000 | SEN62 |
| PM1 `0x042C` | 4 = UGM3 | 0 .. 1000 | SEN62 |
| CO2 `0x040D` | 0 = PPM | 400 .. 5000 | Senseair Sunrise |
| TVOC `0x042E` | 1 = PPB | 0 .. 500 | SGP40 **VOC Index** |

**The TVOC row is not what it appears to be.** The SGP40 reports an index from
1 to 500 where 100 is this room's rolling 24 hour average. It is not a
concentration and there is no conversion. The firmware publishes the raw index
in a cluster that expects ppb, because a controller that gets no number can
build no automation, and the brief explicitly asks for VOC automations.

This is written down in four places - here, in `docs/APPLE_HOME.md`, in
`ac_matter.cpp` and in `docs/DASHBOARD_INTERFACE.md` - and it is switchable with
`voc_publish_index_as_ppb`. See EDR-10 in `docs/ENGINEERING_DECISIONS.md` for
the full argument.

`LevelIndication` is not enabled on any of them: a coarse five-step enum
derived from a measured ug/m3 adds nothing the Air Quality cluster on the same
endpoint does not already carry.

### Temperature and humidity, endpoints 2 and 3

From the SHT40, read every 10 s together with the SGP40 (EDR-17). The SEN62's
own T/RH is not used.

### Power sources: the cells on endpoint 0, USB-C on its own endpoint

Since v1.4 the device runs from USB-C with four LiFePO4 cells, charged in the
device, as the backup (EDR-21). Two Power Source clusters describe that, one
per endpoint (one Power Source cluster per endpoint is all Matter allows):

| | endpoint 0 | the USB-C endpoint |
|---|---|---|
| `Description` | "LiFePO4 1S4P" | "USB-C" |
| `Order` | 1 | 0 (preferred) |
| features | `BAT` (Battery), `RECHG` (Rechargeable), `REPLC` (Replaceable) | `WIRED` |

| where the power comes from | endpoint 0 `Status` | `BatPresent` | USB-C `Status` / `WiredPresent` |
|---|---|---|---|
| the cells (`power: cells`) | 1 Active | true | 3 Unavailable / false |
| USB-C in J2, cells in (`power: USB-C, cells ...`) | 2 Standby | true | 1 Active / true |
| USB-C in J2, no cells (`power: USB-C, no cells`) | 3 Unavailable | false | 1 Active / true |

USB-C is recognised from the PWR-K node on GPIO4 (the #6091's input). A
computer on the FireBeetle's own USB-C also counts as external power for the
USB-C endpoint and the measurement engine, but it never charges the cells and
leaves endpoint 0 on 1 Active. The cells count as absent below 1.0 V. On
external power the device enters no low or critical battery state, but
`BatChargeLevel` still reports the cells' level.

**Battery, endpoint 0:**

| attribute | what it carries |
|---|---|
| `Status` | 1 Active, 2 Standby, 3 Unavailable (table above) |
| `Description` | "LiFePO4 1S4P" |
| `BatPresent` | true; false on USB-C without cells |
| `BatPercentRemaining` | half-percent units, coarse, from the cell voltage (`pwr_std`, TI's LFP curve). LiFePO4 is flat between about 3.26 and 3.33 V: trust it for "full" and "nearly empty". 100 % while full or paused on USB-C. Null without cells |
| `BatVoltage` | cell voltage in mV (the four cells are in parallel) |
| `BatChargeLevel` | 0 OK, 1 Warning (below 3.20 V, about 10 %), 2 Critical (below 3.10 V, about 6 %; measuring stops), with 0.05 V hysteresis |
| `BatChargeState` | 0 Unknown, 1 IsCharging, 2 IsAtFullCharge (also during the charge pause), 3 IsNotCharging. **IsCharging marks the window in which temperature and humidity may read a little high** (charger heat, 0.85–1.7 W); nothing is compensated |
| `BatFunctionalWhileCharging` | true |
| `BatReplacementNeeded` | true on a latched charger fault: the safety timer ran out twice in one USB session, or an ISET / over-current fault. Unplug and replug USB-C; if it recurs, see `docs/TROUBLESHOOTING.md` |
| `BatReplaceability` | 2 UserReplaceable - two screws on the battery door, see `docs/ASSEMBLY.md` |
| `BatReplacementDescription` | "4 x AER18650m2A2 LiFePO4, all at once" |
| `BatQuantity` | 4 |
| `BatCommonDesignation` | `0x4C` (18650) |
| `BatApprovedChemistry` | `0x14` (LithiumIronPhosphate) |

**USB-C endpoint:** `Status` (1 Active while powered from USB-C, 3
otherwise), `WiredPresent`, `WiredCurrentType` 1 (DC), `Description`
"USB-C", `Order` 0.

## ICD - this is a battery device

Configured as a **Short Idle Time (SIT) ICD**:

| parameter | value |
|---|---|
| fast poll | 500 ms |
| slow poll | 15 000 ms |
| active mode duration | 1 000 ms |
| active mode threshold | 1 000 ms |
| idle mode duration | 60 s |

15 s is the maximum a SIT ICD is allowed. It is also what
`docs/BATTERY_LIFE.md` assumes, and `ac_config_validate()` refuses to accept a
larger value at runtime so the two cannot drift apart.

A **Long Idle Time (LIT)** configuration ships as
`firmware/sdkconfig.defaults.lit` and is labelled experimental, because Matter
1.4 says a LIT ICD "SHALL operate as a SIT ICD if it doesn't have at least one
registration with any client on any fabric" - whether a given controller
registers is not something this project can promise.

## Reporting policy

Writing a Matter attribute wakes the radio and sends to every subscriber, so
the firmware only publishes when at least one value has moved past its
threshold since the last publish; a float that has not changed is not
rewritten:

| attribute | publish when it moves by |
|---|---|
| PM2.5, PM1 | 0.5 ug/m3 |
| PM10 | 1.0 ug/m3 |
| CO2 | 15 ppm |
| VOC index | 5 points |
| temperature | 0.2 degC |
| humidity | 1 % |
| battery percentage | 1 % |
| air quality | any change of state |

The Power Source attributes - both endpoints - are written separately, on
every battery read (`battery_interval_s`, 5 min by default).

## Multi-admin (the dashboard)

The sensor can belong to several fabrics at once (`CONFIG_MAX_FABRICS=5`).
Apple Home is one; the e-ink dashboard is another. Share the sensor from the
Home app with "Turn On Pairing Mode". Everything the dashboard needs is in
`docs/DASHBOARD_INTERFACE.md`.

## Identify

Identify blinks the status LED white. With two identical units, that is how
to tell which box is which.

## Commissioning

Standard Matter BLE commissioning. On first boot the device advertises for
15 minutes; afterwards a 3 to 8 second button press re-opens the window for
5 minutes (the LED blinks blue) and prints the manual pairing code to the serial log.

**The pairing code is not hard-coded.** The build uses esp-matter's default
test credentials for development, and
`tools/commissioning/make_label.py` generates the QR code and manual code from
the device's actual commissioning payload for the label you stick on the case.
For anything beyond your own workshop, use esp-matter's
`mfg_tool` to generate per-device passcodes and discriminators into the
`fctry` partition.

## Firmware update

Matter OTA is compiled in (`CONFIG_ENABLE_OTA_REQUESTOR=y`) and the partition
table has two 1.9 MB app slots plus `otadata`, so an update can be applied and
rolled back. Apple Home does not currently act as an OTA provider, so in
practice updates go over USB-C:

```bash
idf.py -p /dev/tty.usbmodem* flash
```

Rollback protection is on (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`): an
image that does not reach a healthy state is reverted on the next boot.

## Factory reset

A 12 to 20 second button press erases the Matter fabric, the Thread
credentials, the configuration, the baseline and the stored history, then
reboots into commissioning mode. The firmware itself is not touched. Holding
the button longer than 20 seconds is ignored, so a jammed button cannot wipe
the device.

## Security

* no cloud service, no account, no telemetry, no external server
* commissioning uses Matter's standard PASE/CASE handshake
* the Matter console and the OpenThread CLI are compiled out
  (`CONFIG_OPENTHREAD_CLI=n`); the service console (`config`, `baseline`,
  `co2`, `events`, `diag`) only starts on external power and is only
  reachable through the FireBeetle's USB-C, so it needs physical access
* the device is read-only over Matter: every attribute it exposes is a
  measurement, and nothing a controller writes changes what it measures
