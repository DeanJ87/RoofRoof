---
description: "Fix common RoofRoof problems: WiFi, the web UI, IPv6, uploads, Alpaca clients, alerts, MQTT and the motor driver's idle state."
---

# Troubleshooting

---

## WiFi Won't Connect

1. Confirm the SSID is 2.4 GHz — ESP32 doesn't support 5 GHz
2. Check password (case-sensitive)
3. Move the device closer to the router for initial setup
4. If the **RoofRoof-Setup** hotspot never appears, wait 45 seconds after power-on with a saved network that can't be joined, or do a full erase and re-flash ([Resetting everything](#resetting-everything))

See [First Boot](../getting-started/first-setup.md).

---

## Web UI Not Loading

**`ERR_CONNECTION_REFUSED` or blank page:**

1. Verify the filesystem was flashed — upload the web UI file on **Updates**, or `pio run --target uploadfs`
2. Check serial logs for `LittleFS mount failed`
3. Re-upload the filesystem (a connection drop during the web UI write leaves it unavailable until a later upload succeeds; the REST API and update endpoints stay reachable)
4. If still broken, flash the release's USB package ([USB flash](../getting-started/usb-flash.md))

---

## Can't Reach the Device Over IPv6

- **System → WiFi** should list a `link-local` address; if it lists none, check **Settings → Network → IPv6** is on and restart the device.
- No `global`/`local` address means the router isn't announcing an IPv6 prefix - link-local still works on the local network (`http://[fe80::...%en0]/` - add your computer's interface after `%`).
- `403 IPv6 requests are only accepted from the local network`: you're coming from outside the device's own network prefix (another VLAN, a VPN, the internet). Use the IPv4 address, or a VPN that puts you on the same network ([IPv6](../user-guide/ipv6.md)).
- Lookups of `roofroof.local` slow or IPv4-only: check mDNS is on; with IPv6 on, the device answers AAAA as well as A.

---

## An Upload Fails

1. Ensure stable power — don't run on a weak USB charger during flash
2. Check WiFi signal strength (RSSI better than -75 dBm)
3. Verify the `.bin` file is the right one: `roofroof-l2-firmware-*` for **Firmware**, `roofroof-l2-littlefs-*` for **Web UI**. The device refuses files for another product or layout and says why ("Not RoofRoof firmware.", "Wrong partition layout").
4. Don't navigate away from the update page during upload

If a new firmware doesn't finish starting (WiFi and the web server up), the device falls back to the previous app slot on its next boot. If it still doesn't respond, reflash via USB ([USB flash](../getting-started/usb-flash.md) - settings are kept).

"Could not activate partition" after an upload usually means the transfer was corrupted on a weak link - upload again. See [Updates](../user-guide/ota.md).

---

## N.I.N.A. Can't See the Device

1. **Settings → Network → ASCOM Alpaca** must be on, and the device restarted since.
2. Discovery is a UDP broadcast, which doesn't cross subnets or VLANs. If N.I.N.A.'s PC is on another network, add the device manually: its IP, port `80`.
3. The server lists one device, the roof's Dome (`curl http://<device-ip>/management/v1/configureddevices`). An empty list means Alpaca is off.
4. N.I.N.A. connects but Open or Close shows an error: the message says why (weather not safe, mount not parked, position unknown, a fault) - see [When a command is refused](../user-guide/alpaca.md#when-a-command-is-refused).

See [ASCOM Alpaca](../user-guide/alpaca.md).

---

## A Connection Is Refused Under Heavy Use

The ESP32's network stack holds at most **16 TCP connections** at once, including ones that have just closed (they linger for a short while). That limit is built into the ESP32 Arduino framework the firmware uses.

What uses connections:

- live updates in open browser tabs: at most 3 per endpoint, however many tabs are open
- MQTT: 1
- an imaging app: 1-2 per request burst (each Alpaca request is a short connection)
- internet alerts: 1 while sending

`/api/status` → `connections` shows who holds what: `liveUpdates` counts the clients, and `replaced` and `stalledClosed` count live-update clients closed since the restart (replaced by a newer one, or closed because they stopped reading).

`tools/soak/connection_soak.py --host <device-ip>` reproduces the worst case against a device and reports every request that failed.

---

## An Alert Didn't Arrive

Open the alerts bell. It lists the recent alerts with each channel's result. Common reasons for no alert:

- Alerts are paused (the bell in the header is crossed out) or **Send alerts** is off
- The event's level is **Off**
- The cooldown held it back; it's sent when the cooldown ends if things haven't changed back
- The channel failed - the bell's list shows each channel's result, e.g. Pushover "user identifier is not a valid user" means the user key (not the app token) is wrong
- The roof's critical alerts (**Mount didn't park**, **Roof didn't close**) ignore the pause, the send mode and **Send alerts**, but still need a channel switched on: with none set up the serial log is the only report. Press **Test** on the roof's rows to prove the channel ([Alerts](../user-guide/alerts.md#the-roof))

---

## MQTT Not Publishing

1. Check `mqtt.enabled` is `true` in config
2. Ping the broker from another device to confirm it's reachable
3. Verify broker address and port
4. Check username/password if your broker requires auth
5. Watch broker logs: `mosquitto_sub -h broker -t "#" -v`

The topics the device publishes are listed in [MQTT](../user-guide/mqtt.md).

---

## The Motor Driver Moves Briefly at Power-On

Until the firmware starts, the ESP32's GPIO25, 26 and 27 float, and a floating enable pin can switch the IBT-2 on. Fit the 10 kΩ pull-down from the enable pin to GND ([Wiring](../hardware/wiring.md#the-en-pull-down)). The firmware drives all three pins low as the first thing `setup()` does, but that is roughly 300 ms after reset.

---

## Commissioning

Symptoms met while following the [commissioning guide](../hardware/commissioning.md). The roof's status (`roof.state`, `roof.fault`, `roof.hall`) is in [`GET /api/status`](../api/rest.md#get-apistatus); a latched fault stays until a person clears it.

| Symptom | Likely cause and what to do |
|---|---|
| The roof moves the wrong way, or `didNotLeave` after the first open | The motor turns the other way: stop it, set `roof.openDirection` to the other value, clear the fault ([Roof motion](../user-guide/roof-motion.md#timeouts-and-faults)) |
| `timeout` before the roof reaches the end | The speed setting is too high or the margin too small: re-measure the speed at 100% ([Calibration](../hardware/calibration.md#travel-and-speed)), or raise `roof.timeoutMarginSeconds`. A slow duty that is not proportional to the duty needs a larger margin |
| `wrongMagnet` | The other end's magnet was read: the Hall polarity is the wrong way round (`roof.hallHighEnd`) or the thresholds are too close to the resting value ([Calibration](../hardware/calibration.md#hall-sensor)) |
| `hallRail`, or `hall.counts` of 0 or 4095 | The Hall sensor is disconnected, shorted or its divider is missing; check the 5 V supply and that the output goes through the divider ([Wiring](../hardware/wiring.md#hall-sensor-ss49e)) |
| `supplyLow` at a motor start, or the open is refused for the supply | The supply dips more than 250 ms at the start or the sense divider is off: check the supply and wiring first; `supplyVolts` of 0 means the divider isn't connected |
| Open refused: weather not safe | No weather source is switched on (that counts as unsafe), SQMeter is unreachable or stale, or it says unsafe: read the reasons in the answer and the `Safety` log lines ([Safety inputs](../user-guide/safety-inputs.md)) |
| Close refused: mount not parked | N.I.N.A.'s Advanced API isn't answering, the mount isn't at park, or a park switch is open or wired the wrong way round; the reasons name which |
| "sources disagree" | N.I.N.A. and a park switch say opposite things: check the switch's wiring, that it closes at park, and N.I.N.A.'s park position |
| Open or close refused with the position unknown | Only a person can move the roof from between the magnets: use the web UI or the REST API, not N.I.N.A. or MQTT |
| The roof doesn't close by itself | A fault is latched, the position is unknown, the person pressed Stop (it stands down until closed or 10 minutes of safe weather), or the mount isn't parked: the alert says which ([Close on unsafe weather](../user-guide/close-on-unsafe.md#alerts)) |
| A restart, upload or WiFi change answers `409` | The roof is moving, a close is waiting, or the automatic close is working; try again once it has closed ([Roof motion](../user-guide/roof-motion.md#while-the-roof-moves)) |
| Open answers `409` `hotspot` | The setup hotspot is on; the roof can't be opened while it is. Join the home network first ([First Boot](../getting-started/first-setup.md)) |
| N.I.N.A.'s Stop shows an error | A close is running in weather that isn't safe: an Alpaca client can't stop it. Use the web UI's Stop ([ASCOM Alpaca](../user-guide/alpaca.md#stop-from-nina-in-bad-weather)) |

---

## Serial Monitor Garbled

Baud rate must be `115200`. In PlatformIO:

```bash
pio device monitor --baud 115200
```

---

## Resetting Everything

Nuclear option — clears firmware, filesystem, and NVS (WiFi config, all settings):

```bash
esptool.py --chip esp32 --port PORT erase_flash
```

Then flash the release's USB package ([Flashing](../getting-started/flashing.md)); the device starts with the setup hotspot.
