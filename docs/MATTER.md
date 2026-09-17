# Matter

Built against **esp-matter release/v1.6** with the Matter 1.6 data model, on
ESP-IDF v5.5.5.

## Endpoints

| ep | device type | id | clusters (server) |
|---|---|---|---|
| 0 | Root Node | `0x0016` | Descriptor, Basic Information, General Commissioning, Network Commissioning, ICD Management, **Power Source** |
| 1 | Air Quality Sensor | `0x002C` | Descriptor, Identify, **Air Quality** `0x005B`, **PM2.5** `0x042A`, **PM10** `0x042D`, **PM1** `0x042C`, **CO2** `0x040D`, **TVOC** `0x042E` |
| 2 | Temperature Sensor | `0x0302` | Descriptor, Identify, Temperature Measurement `0x0402` |
| 3 | Humidity Sensor | `0x0307` | Descriptor, Identify, Relative Humidity Measurement `0x0405` |

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

All five are created with the `NumericMeasurement` (`MEA`) feature only, with
`MeasurementMedium` = Air.

| cluster | unit attribute | range advertised | source |
|---|---|---|---|
| PM2.5 `0x042A` | 4 = UGM3 | 0 .. 1000 | SPS30 |
| PM10 `0x042D` | 4 = UGM3 | 0 .. 1000 | SPS30 |
| PM1 `0x042C` | 4 = UGM3 | 0 .. 1000 | SPS30 |
| CO2 `0x040D` | 0 = PPM | 400 .. 5000 | SCD41 |
| TVOC `0x042E` | 1 = PPB | 0 .. 500 | SGP40 **VOC Index** |

**The TVOC row is not what it appears to be.** The SGP40 reports an index from
1 to 500 where 100 is this room's rolling 24 hour average. It is not a
concentration and there is no conversion. The firmware publishes the raw index
in a cluster that expects ppb, because a controller that gets no number can
build no automation, and the brief explicitly asks for VOC automations.

This is written down in four places - here, in `docs/APPLE_HOME.md`, in
`ac_matter.cpp` and on the device's own screen - and it is switchable with
`voc_publish_index_as_ppb`. See EDR-10 in `docs/ENGINEERING_DECISIONS.md` for
the full argument.

`LevelIndication` is not enabled on any of them: a coarse five-step enum
derived from a measured ug/m3 adds nothing the Air Quality cluster on the same
endpoint does not already carry.

### Power Source, endpoint 0

| attribute | what it carries |
|---|---|
| `Status` | 1 Active, or the charging state |
| `BatPercentRemaining` | state of charge from the MAX17048, in half-percent units |
| `BatVoltage` | cell voltage in mV |
| `BatChargeLevel` | 0 OK, 1 Warning (<= 20 %), 2 Critical (<= 5 %) |
| `BatReplaceability` | 2 UserReplaceable - and it genuinely is, see `docs/ASSEMBLY.md` |

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
the firmware compares before it writes:

| attribute | reported when it moves by |
|---|---|
| PM2.5, PM1 | 0.5 ug/m3 |
| PM10 | 1.0 ug/m3 |
| CO2 | 15 ppm |
| VOC index | 5 points |
| temperature | 0.2 degC |
| humidity | 1 % |
| battery | 1 % |
| air quality | any change of state |

## Commissioning

Standard Matter BLE commissioning. On first boot the device advertises for
15 minutes; afterwards a 3 to 8 second button press re-opens the window for
5 minutes and puts the manual pairing code on the screen.

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
  (`CONFIG_OPENTHREAD_CLI=n`); the USB serial console is a log output only and
  accepts no commands
* the device is read-only over Matter: every attribute it exposes is a
  measurement, and nothing a controller writes changes what it measures
