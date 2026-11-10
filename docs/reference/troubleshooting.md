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
3. Today the Alpaca server lists no devices (`curl http://<device-ip>/management/v1/configureddevices` returns an empty list), so a client finds the server but has nothing to connect to. The roof's device *comes in a later spec*.

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
- The roof's own events *come in later specs*; today only the imaging-app events, "Alerts resumed" and tests exist ([Alerts](../user-guide/alerts.md))

---

## MQTT Not Publishing

1. Check `mqtt.enabled` is `true` in config
2. Ping the broker from another device to confirm it's reachable
3. Verify broker address and port
4. Check username/password if your broker requires auth
5. Watch broker logs: `mosquitto_sub -h broker -t "#" -v`

Today the device publishes only `availability`, `alerts` and `alerts/armed` ([MQTT](../user-guide/mqtt.md)).

---

## The Motor Driver Moves Briefly at Power-On

Until the firmware starts, the ESP32's GPIO25, 26 and 27 float, and a floating enable pin can switch the IBT-2 on. Fit the 10 kΩ pull-down from the enable pin to GND ([Wiring](../hardware/wiring.md#the-en-pull-down)). The firmware drives all three pins low as the first thing `setup()` does, but that is roughly 300 ms after reset.

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
