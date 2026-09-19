# Thread

## Role

The AIR CHECK is a **Sleepy End Device** - an OpenThread MTD
(`CONFIG_OPENTHREAD_MTD=y`) that spends almost all of its time with the radio
off and polls its parent router for anything waiting.

```
      radio off, ~55 uA        poll, a few ms at up to 344 mA
    |---------------------|--|---------------------|--|--------
    <------- 15 s -------->
```

It never routes for anyone else, never becomes a leader, and does not extend
the mesh. That is the correct role for something running off six AA cells.
It stays a sleepy end device on a USB-C charger too: the role is compiled
in, and the cells may take over at any moment.

## Border router

You need one, and Apple sells three: **HomePod mini, HomePod (2nd gen), and
Apple TV 4K (2nd gen Wi-Fi + Ethernet or newer)**. The AIR CHECK joins the
Thread network that your HomePod or Apple TV runs, and reaches Apple Home
through it. There is no fallback - this device has no Wi-Fi (it is compiled
out) and no Ethernet.

If you have several Apple border routers they cooperate; you do not need to
pick one.

## Why Wi-Fi is not in the build

The ESP32-C6 has a Wi-Fi 6 radio. It is disabled at compile time
(`CONFIG_ENABLE_WIFI_STATION=n`, `CONFIG_ENABLE_WIFI_AP=n`,
`CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n`) for three reasons: a Wi-Fi Matter device
cannot be a sleepy device, and the whole battery budget rests on that; the
Wi-Fi stack costs flash that the 4 MB part needs for two OTA slots; and
leaving a radio in a build that must not be used is an invitation.

Bluetooth LE stays in, because Matter commissioning needs it. It is only
active during commissioning.

## Power

Espressif's own measurement of their Matter ICD example on an ESP32-C6
DevKitC, at a 5 s poll and 20 dBm:

* average **174 uA**
* floor between polls **55 uA**
* peak **344 mA** during a poll

From that trace, one poll costs about 0.6 mC, which at the 15 s slow poll this
firmware uses would be roughly 95 uA. An independent one-hour PPK2 measurement
at exactly 15 s came out higher, **121.9 uA average, 39.3 uA floor**, and that
is the number in `docs/BATTERY_LIFE.md` (EDR-13).

The 344 mA peak comes out of the FireBeetle's 3.3 V buck, which is fed from
VSYS - the cells' Pololu regulator at 3.90 V, or the USB-C branch's at
4.20 V - not from the cells directly; `design.py` checks the worst-case load
on that rail against the regulator's rating. The
SEN62's switch (Pololu #2810) has no soft start, so its switch-on step also
lands on the 3.3 V buck - verify that on the bench (TESTING T-P3).

## Reducing TX power

20 dBm is the default and is more than a device in the same room as its border
router needs. Dropping to 14 dBm saves some radio energy - but the whole
ESP32-C6 + Thread line is only about 6 % of the ECO budget
(`docs/BATTERY_LIFE.md`), so expect a small gain:

```
idf.py menuconfig
  -> Component config -> OpenThread -> Thread Core Features
     -> Thread Radio TX power
```

Check the link quality in the serial log afterwards. Below about
-85 dBm you are trading reliability for a fraction of a percent of runtime,
which is a bad trade.

## Joining, and re-joining

Commissioning hands the device the network's operational dataset, which is
stored in NVS. After a power cut it re-attaches on its own, typically within a
few seconds. If it cannot find its parent it retries with a backoff; the
serial log shows the attach state. A dashboard notices a detached sensor
because its reads time out.

Losing the Thread network does not stop anything. The device keeps measuring,
keeps updating its baseline, keeps recording events and keeps its history
current. It catches up when the network comes back - but note that Matter has
no store-and-forward: readings taken while offline are not backfilled into
Apple Home. They are in the device's own 7 day history.

## Two devices on one network

Two AIR CHECKs are two ordinary sleepy end devices. Thread handles dozens of
them. They do not talk to each other and do not interfere; each polls its own
parent independently.

## What has not been verified

Everything in this document about the *device* comes from the source in this
repository and from Espressif's published measurements. Everything about
**Apple's border routers** comes from Apple's own documentation and has not
been tested by the authors against a real HomePod. See `docs/TESTING.md` for
the honest status of each claim.
