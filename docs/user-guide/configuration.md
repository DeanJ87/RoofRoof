---
description: "Configure RoofRoof from the web UI or REST API: device name, network, MQTT, time, password protection and alerts. Settings live in NVS."
---

# Configuration

All settings are stored in NVS (Non-Volatile Storage) and survive firmware and filesystem updates. Configure via the web UI (**Settings**) or the REST API. The roof, weather and park settings are on the **Roof** tab ([below](#safety-and-park) and in the [reference](#full-configuration-reference)).

## Settings pages

The [setup wizard](setup-wizard.md) walks through the essential settings once, with live readings and a review; it saves through the same settings API. **Settings** has five tabs. Changes are kept until you **Save**; a tab with a problem shows a red dot, and settings that only take effect after a restart ask for one when you save.

Some settings only work when something else is on - MQTT alerts need MQTT, internet alerts need WiFi. Such a setting can't be switched on while what it needs is off, and if you switch the other thing off later the setting is kept exactly as it was but marked **Inactive** with the reason and a button that takes you to the fix. The device doesn't act on it until then, and nothing has to be set up again when you switch the other thing back on. Before you save, a note lists anything the change would make inactive. Every such setting is listed in [Settings dependencies](../reference/settings-dependencies.md).

=== "Device"
    Name, password protection, command-line uploads (ArduinoOTA) and Backup & restore.

    ![Device settings](../assets/screenshots/settings-device.png)

=== "Roof"
    How the roof moves, the Hall sensor, current sense and the supply, the weather sources, the mount park and the daylight failsafe:

    - **Roof motion**: travel, speed, timeout margin, the time to leave a magnet, ramps, top and slow duty, and the opening direction - see [Roof motion](roof-motion.md).
    - **Hall sensor**: which end reads high and the two thresholds, with the live reading drawn against the thresholds as you type them - see [Calibration](../hardware/calibration.md).
    - **Current and supply**: current sense and its limits, and the minimum supply.
    - **Weather sources**: SQMeter over HTTP and over MQTT, the rain contact and the stale time - see [Safety inputs](safety-inputs.md).
    - **Mount park**: the N.I.N.A. address, the park switches fitted and the **Park time limit** of the [close on unsafe weather](close-on-unsafe.md).
    - **Daylight**: the mode (**Off**, **Alert**, **Alert and close**), the **Sun altitude** threshold and the site's latitude and longitude - see the [daylight failsafe](close-on-unsafe.md#daylight-failsafe). The **Daylight band** is under **Advanced**.
    - **Automatic close**: how the [close on unsafe weather](close-on-unsafe.md#settings-of-the-automatic-close) behaves - the safe spell that re-arms it after a Stop, the reminder interval, what happens when a close doesn't start and whether a Stop from an imaging app or MQTT calls it off. The timings that rarely need changing are under **Advanced**. While Alpaca is off a note says imaging apps can't open or close the roof, with a button to the [ASCOM Alpaca](alpaca.md#nina-opens-and-closes-the-roof) card.
    - **Ask before Open and Close** (in **Roof motion**): Open and Close on the dashboard ask once before they are sent. Stop never asks.

    While the roof is opening or closing the tab says so: the device refuses every save until it stops. Saving while the roof is stopped part-way makes its position unknown, and the tab says that too.

    ![Roof settings](../assets/screenshots/settings-roof.png)

=== "Network"
    WiFi, mDNS, IPv6, MQTT including Home Assistant discovery - see [MQTT](mqtt.md) - and ASCOM Alpaca - see [ASCOM Alpaca](alpaca.md).

    ![Network settings](../assets/screenshots/settings-network.png)

=== "Time"
    NTP and the time zone.

    ![Time & Location settings](../assets/screenshots/settings-time.png)

=== "Alerts"
    Events, levels, wording and channels - see [Alerts](alerts.md).

    ![Alert settings](../assets/screenshots/settings-alerts.png)

---

## Full Configuration Reference

`GET /api/config` returns every setting (secrets masked). The fields, with their defaults:

```json
{
  "deviceName": "RoofRoof",
  "wifi": {
    "ssid": "YourWiFiSSID",
    "password": "YourWiFiPassword",
    "hostname": "roofroof",
    "mdns": true,
    "ipv6": true,
    "autoReconnect": true,
    "reconnectDelayMs": 1000,
    "maxReconnectDelayMs": 300000
  },
  "ntp": {
    "enabled": true,
    "server1": "pool.ntp.org",
    "server2": "time.nist.gov",
    "timezone": "UTC0",
    "syncIntervalMs": 600000
  },
  "mqtt": {
    "enabled": false,
    "broker": "",
    "port": 1883,
    "username": "",
    "password": "",
    "topic": "roofroof",
    "publishIntervalMs": 60000,
    "homeAssistant": { "enabled": false, "discoveryPrefix": "homeassistant" },
    "roofCommands": false,
    "changeMinSeconds": 1
  },
  "ota": { "enabled": false, "password": "" },
  "auth": { "enabled": false, "username": "admin", "password": "" },
  "alpaca": { "enabled": false },
  "safety": {
    "http": { "enabled": false, "url": "" },
    "mqtt": { "enabled": false, "topic": "sqmeter" },
    "staleSeconds": 30,
    "rainContact": false
  },
  "park": {
    "nina": { "url": "", "api": "v2" },
    "switches": "none"
  },
  "alerts": {
    "enabled": false,
    "sendMode": "any",
    "cooldownSeconds": 300,
    "clientSilentSeconds": 120,
    "events": {
      "mount_not_parked": { "level": 4, "sound": "", "title": "", "message": "" },
      "roof_not_closed": { "level": 4, "sound": "", "title": "", "message": "" },
      "move_fault": { "level": 3, "sound": "", "title": "", "message": "" },
      "supply_low": { "level": 2, "sound": "", "title": "", "message": "" },
      "unsafe_closing": { "level": 2, "sound": "", "title": "", "message": "" },
      "roof_closed": { "level": 2, "sound": "", "title": "", "message": "" },
      "daylight_open": { "level": 3, "sound": "", "title": "", "message": "" },
      "daylight_closed": { "level": 2, "sound": "", "title": "", "message": "" },
      "daylight_unknown": { "level": 2, "sound": "", "title": "", "message": "" },
      "safety_lost": { "level": 2, "sound": "", "title": "", "message": "" },
      "client_lost": { "level": 3, "sound": "", "title": "", "message": "" },
      "client_back": { "level": 1, "sound": "", "title": "", "message": "" },
      "client_disconnected": { "level": 0, "sound": "", "title": "", "message": "" }
    },
    "pushover": { "enabled": false, "userKey": "", "appToken": "", "sound": "" },
    "ntfy": { "enabled": false, "server": "https://ntfy.sh", "topic": "", "token": "" },
    "webhook": { "enabled": false, "url": "", "authHeader": "", "insecureTls": false },
    "mqtt": { "enabled": false }
  },
  "roof": {
    "travelMm": 1850,
    "speedMmPerSecond": 38,
    "timeoutMarginSeconds": 15,
    "leaveMagnetSeconds": 10,
    "rampUpMs": 2000,
    "rampDownMs": 3000,
    "topDutyPercent": 100,
    "slowDutyPercent": 40,
    "openDirection": "forward",
    "hallHighEnd": "open",
    "hallHighCounts": 3000,
    "hallLowCounts": 1100,
    "currentSenseEnabled": false,
    "maxCurrentAmps": 10,
    "currentAmpsPerVolt": 8.5,
    "minRunningAmps": 0,
    "minSupplyVolts": 11
  },
  "daylight": { "mode": "alert", "sunAltitudeDeg": 0, "bandDeg": 0.5 },
  "location": { "set": false, "latitude": 0, "longitude": 0 },
  "autoClose": {
    "rearmMinutes": 10,
    "parkRequestSeconds": 30,
    "parkHoldSeconds": 4,
    "retryMinutes": 5,
    "closeStartSeconds": 30,
    "closeGuardSeconds": 30,
    "notStarted": "halt",
    "remindMinutes": 30,
    "remoteStop": false
  },
  "ui": { "confirmOpenClose": false }
}
```

The JSON Schema is `config.schema.json`; the [API Reference](../api/reference.md) links it from `GET /api/config`.

## Fields

### Device

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `deviceName` | string | `"RoofRoof"` | Friendly name shown in the web UI, in alerts and as the Alpaca server name |

### WiFi

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ssid` | string | — | Network name (2.4 GHz only) |
| `password` | string | — | Network password |
| `hostname` | string | `"roofroof"` | Network name: `hostname.local` via mDNS and the DHCP host name. Letters, numbers and hyphens, up to 32 |
| `mdns` | bool | `true` | Advertise `hostname.local` and the web service via mDNS |
| `ipv6` | bool | `true` | IPv6 alongside IPv4 on the joined network; applies after a restart - see [IPv6](ipv6.md) |
| `autoReconnect` | bool | `true` | Reconnect on WiFi drop |
| `reconnectDelayMs` | int | `1000` | Initial reconnect delay (ms) |
| `maxReconnectDelayMs` | int | `300000` | Max reconnect backoff — 5 min |

### NTP

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `true` | Enable NTP time sync |
| `server1` | string | `"pool.ntp.org"` | Primary NTP server |
| `server2` | string | `"time.nist.gov"` | Fallback NTP server |
| `timezone` | string | `"UTC0"` | POSIX timezone string |
| `syncIntervalMs` | int | `600000` | Re-sync interval — default 10 minutes |

!!! tip "POSIX timezone strings"
    Examples: `GMT0`, `EST5EDT,M3.2.0,M11.1.0`, `PST8PDT,M3.2.0,M11.1.0`, `CET-1CEST,M3.5.0,M10.5.0/3`.
    A full list is available at [timezonedb.com](https://timezonedb.com).

### MQTT

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Enable MQTT |
| `broker` | string | — | Broker hostname, IPv4 address, or IPv6 address (`fd00::10`, or `[fd00::10]:1883` to give the port here) - see [IPv6](ipv6.md) |
| `port` | int | `1883` | Broker port |
| `username` | string | `""` | Auth username (leave empty if none) |
| `password` | string | `""` | Auth password |
| `topic` | string | `"roofroof"` | Base topic: letters, numbers, `_`, `-`, with `/` between levels. See [MQTT](mqtt.md#topics) |
| `publishIntervalMs` | int | `60000` | How often the roof's state document is republished at rest ([MQTT](mqtt.md#the-roof-document)) |
| `homeAssistant.enabled` | bool | `false` | Home Assistant MQTT discovery |
| `homeAssistant.discoveryPrefix` | string | `"homeassistant"` | Discovery prefix |
| `roofCommands` | bool | `false` | Accept open, close and stop on `<topic>/roof/set`, with an Alpaca client's limits ([Roof commands](mqtt.md#roof-commands)). Config saved before this setting existed loads with it off |
| `changeMinSeconds` | int | `1` | At rest, a change of the roof's state document is published at most this often (1-60 s). While the roof moves or a close waits the fixed busy cadence applies ([MQTT](mqtt.md#the-roof-document)). Shown as **MQTT change gap** in **Settings → Roof → Automatic close → Advanced** |

### ArduinoOTA

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Enable command-line ArduinoOTA uploads |
| `password` | string | `""` | Needed for command-line ArduinoOTA to start |

ArduinoOTA only runs when `ota.enabled` is `true` and `ota.password` is set. Switched on without a password it is saved but inactive ("Set an upload password"). The web UI upload page ([Updates](ota.md)) is separate from command-line ArduinoOTA.

### HTTP Authentication

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Require credentials for mutation endpoints |
| `username` | string | `"admin"` | Username for HTTP Basic Auth |
| `password` | string | `""` | Password - a save with auth on and no password is refused: "HTTP auth password is required when auth is enabled" |

When `auth.enabled` is `true`, the endpoints marked "required" in the [Security](security.md#protected-endpoints-when-auth-is-enabled) table need HTTP Basic Auth credentials. Read-only endpoints (`GET /api/status`, `GET /api/config`, `/ws/status`, ...) remain accessible without credentials.

The `auth.password` field is masked in `GET /api/config` responses. Send the mask placeholder or an empty string to preserve the stored password; send `null` to clear it.

### ASCOM Alpaca

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `alpaca.enabled` | bool | `false` | Serve Alpaca discovery, the management API and the roof's Dome, which lets imaging apps open, close and stop the roof within the safety rules (a restart starts or stops the discovery listener). See [ASCOM Alpaca](alpaca.md) |

### Safety and park

Where the weather verdict and the park confirmation come from. See [Safety inputs](safety-inputs.md) for how they are combined. There is no settings page for these yet (it comes with the roof settings); set them through `POST /api/config` or a restored backup.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `safety.http.enabled` | bool | `false` | Poll SQMeter's `GET /api/safety` every 5 s |
| `safety.http.url` | string | `""` | Its full URL, `http://` only, e.g. `http://sqmeter.local/api/safety` |
| `safety.mqtt.enabled` | bool | `false` | Read SQMeter's safety topics from this device's MQTT broker (needs MQTT on) |
| `safety.mqtt.topic` | string | `"sqmeter"` | SQMeter's base topic |
| `safety.staleSeconds` | int | `30` | SQMeter over HTTP counts as unsafe after this long without a good reply (10-600) |
| `safety.rainContact` | bool | `false` | A rain contact is fitted on GPIO4 |
| `park.nina.url` | string | `""` | N.I.N.A. Advanced API base, `http://` only, e.g. `http://192.168.1.20:1888`; empty means not configured, so the mount never counts as parked |
| `park.nina.api` | string | `"v2"` | The API version; only `v2` for now |
| `park.switches` | string | `"none"` | Park switches fitted: `none`, `one` (GPIO32) or `two` (GPIO32 and GPIO33) |
| `park.timeoutSeconds` | int | `240` | How long the [close on unsafe weather](close-on-unsafe.md) waits for the park to be confirmed before a critical alert (30-1800). Set it a little above the longest time your mount takes to park |

### Roof

How the roof moves; see [Roof motion](roof-motion.md). The defaults are cautious until the roof is
measured. A configuration saved by an earlier release loads with these defaults, with current
sense off.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `roof.travelMm` | int | `1850` | Distance between the two ends, in mm (100-20000) |
| `roof.speedMmPerSecond` | number | `38` | Roof speed at 100% duty, in mm/s (1-500) |
| `roof.timeoutMarginSeconds` | int | `15` | Added to a move's expected time to give its hard timeout (3-300) |
| `roof.leaveMagnetSeconds` | int | `10` | A move from an end must leave its magnet within this time (2-120) |
| `roof.rampUpMs` | int | `2000` | Soft start, slow duty to top duty (0-30000) |
| `roof.rampDownMs` | int | `3000` | Slowing near the end: the zone is the distance covered at the top duty in this time (0-30000) |
| `roof.topDutyPercent` | int | `100` | Highest motor duty (20-100) |
| `roof.slowDutyPercent` | int | `40` | Duty at the ends and for every move from an unknown position (10-100, not above the top duty) |
| `roof.openDirection` | string | `"forward"` | Which motor direction opens the roof: `forward` (RPWM) or `reverse` (LPWM) |
| `roof.hallHighEnd` | string | `"open"` | Which end's magnet reads high on the Hall sensor: `open` or `closed` |
| `roof.hallHighCounts` | int | `3000` | Raw reading at or above which the high end's magnet is read (100-3995) |
| `roof.hallLowCounts` | int | `1100` | Raw reading at or below which the other end's magnet is read (100-3995, at least 80 below the high threshold) |
| `roof.currentSenseEnabled` | bool | `false` | Use the IBT-2's current outputs for over-current detection; off until they are wired |
| `roof.maxCurrentAmps` | number | `10` | Motor current that stops the roof as a fault, with current sense on (0.5-40) |
| `roof.currentAmpsPerVolt` | number | `8.5` | Motor amps per volt at the IBT-2's current-sense output, before the divider (0.5-100); set it by [calibration](../hardware/calibration.md#current-sense) |
| `roof.minRunningAmps` | number | `0` | With current sense on, a running current below this after a second of driving counts as a broken sense wire (`currentUnreadable`); 0 = off (0-20). Set it from a [bench measurement](../hardware/calibration.md#current-sense) |
| `roof.minSupplyVolts` | number | `11` | Supply below which a move is refused, or stopped as a fault (6-15) |

### Daylight and location

The [daylight failsafe](close-on-unsafe.md#daylight-failsafe): what happens when the sun is up and the roof isn't closed. A configuration saved by an earlier release loads with these defaults, so an update never turns the close on.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `daylight.mode` | string | `"alert"` | `off` (nothing), `alert` (an alert, nothing moves) or `close` (shown as "Alert and close": park and close as for unsafe weather, and refuse opening while the sun is up). Any other value is refused |
| `daylight.sunAltitudeDeg` | number | `0` | The sun counts as up at or above this altitude, in degrees (-18 to 30); it counts as down again `bandDeg` below it |
| `daylight.bandDeg` | number | `0.5` | Once it is day, night comes again only this far below the threshold, so a sun lingering at it can't flicker the state (0.1 to 2 degrees) |
| `location.set` | bool | `false` | The latitude and longitude below are valid. Without them (or before the clock is set) the daylight check does nothing, and with the mode on it alerts after 10 minutes |
| `location.latitude` | number | `0` | Degrees, north positive (-90 to 90) |
| `location.longitude` | number | `0` | Degrees, east positive (-180 to 180) |

A value out of range is refused with the range. They are edited in **Settings → Roof → Daylight**.

### Setup wizard

Where the [setup wizard](setup-wizard.md) stands (spec 015). These move nothing and need no restart. **Settings** never sends them, so a save there keeps them.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `setup.state` | string | `"new"` | `new` (the wizard opens on the first dashboard visit of a page load), `offered` (a dashboard banner offers it) or `done`. Settings saved by a release before the wizard load as `offered`, so an upgraded roof is never interrupted. Any other value is refused |
| `setup.step` | integer | `0` | The page to resume at, 0 (Device) to 9 (Review) |

### Automatic close

How the [close on unsafe weather](close-on-unsafe.md#settings-of-the-automatic-close) (and the daylight close) behaves. The defaults are the values the device has always used, and a configuration saved by an earlier release loads with them. Each range keeps the safety rules: no value lets the roof close onto an unparked mount, open by itself or stay open in unsafe weather without a critical alert.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `autoClose.rearmMinutes` | int | `10` | **Safe spell to re-arm** (5-60 min): after a person's Stop, a halted close or "can't close by itself", the automatic close acts again only once the roof is closed or the weather has been safe this long; a safe spell shorter than this doesn't restart an attempt's park time limit |
| `autoClose.remindMinutes` | int | `30` | **Reminder interval** (10-120 min): a critical alert repeats this often while the roof stays open after it. The same event and cause alerts at most this often, and an alert no channel took is resent at gaps growing up to this, but neither waits longer than 30 minutes. Never off |
| `autoClose.notStarted` | string | `"halt"` | **Close didn't start**: a close held back (by an upload, an MQTT connect or a network change) that hasn't started within the close start limit. `halt` stops and waits for a person or the safe spell; `retry` calls it off, raises "Roof didn't close" and tries again after the back-off, with the park confirmed again |
| `autoClose.remoteStop` | bool | `false` | **Remote Stop calls off a close**: off, a Stop from an imaging app (Alpaca) or MQTT is refused while a close runs in unsafe weather or in daylight with "Alert and close". On, it is accepted and counts exactly as a person's Stop: the automatic close stands down and raises its critical "Automatic close stopped" alert (saying an imaging app or MQTT stopped it), with the reminders. Needs Alpaca on or MQTT roof commands on |
| `autoClose.parkRequestSeconds` | int | `30` | **Park request spacing** (10-120 s): time between park requests while waiting for the park |
| `autoClose.parkHoldSeconds` | int | `4` | **Park hold** (4-30 s): the park must be confirmed this long without a break before the close is commanded; the close's own park check still applies |
| `autoClose.retryMinutes` | int | `5` | **Back-off** (1-30 min): the wait after a park that wasn't confirmed or a close that failed before the next try |
| `autoClose.closeStartSeconds` | int | `30` | **Close start limit** (25-120 s): a held-back close must start within this. Never below 25 s, so a network change (which holds a close up to 15 s) isn't a failure |
| `autoClose.closeGuardSeconds` | int | `30` | **Close guard** (10-120 s): added to a close's own timeout; a close still moving after both is stopped. The roof's own timeout always stops it first |
| `ui.confirmOpenClose` | bool | `false` | **Ask before Open and Close**: the dashboard's Open and Close ask once before they are sent. Stop never asks |

The last five `autoClose` fields are under **Settings → Roof → Automatic close → Advanced**. A value out of range is refused with the range.

Some choices stay fixed whatever the settings, because the safety rules need them: opening is refused while the setup hotspot is on (it has no password, and Alpaca has no login); a close is refused in the moment before a restart (the reset would cut it off part-way and leave the position unknown); the roof never moves by itself from an unknown position or with a fault latched; and a person's Stop always works.

### Alerts

See [Alerts](alerts.md) for events, levels and channels.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `false` | Send alerts at all |
| `events.<event>.level` | int | see above | 0 off, 1 quiet, 2 normal, 3 urgent, 4 wake me; `mount_not_parked` and `roof_not_closed` 3 or 4 only |
| `events.<event>.sound` | string | `""` | Pushover sound; empty uses `pushover.sound` |
| `events.<event>.title` / `.message` | string | `""` | Your own wording with `{variables}` (up to 80 / 240 characters); empty uses the default |
| `sendMode` | string | `"any"` | When to send: `any` (any time, unless paused) or `whileConnected` (only while an imaging app has an Alpaca device connected) |
| `clientSilentSeconds` | int | `120` | "The imaging app stops checking" after this long without a request (30-3600) |
| `cooldownSeconds` | int | `300` | Minimum gap between alerts of the same kind (0-86400) |
| `pushover`, `ntfy`, `webhook`, `mqtt` | object | off | Channel settings; secrets are masked in `GET /api/config` |

Events: the roof's `mount_not_parked`, `roof_not_closed` (critical: sent whatever `enabled`, the pause or the send mode say), `move_fault`, `supply_low`, `unsafe_closing`, `roof_closed`, `daylight_open`, `daylight_closed`, `daylight_unknown`, `safety_lost`; the imaging app's `client_lost`, `client_back`, `client_disconnected`. All the custom wording together must fit the device's storage: very long texts on many events (or very long channel settings) are refused with "the alert settings are too long to store". Paused or sending is live state, not a setting - see [When to send](alerts.md#when-to-send).

## Security Notes

RoofRoof is designed for a trusted LAN environment. See the [Security Guide](security.md) for a full threat model and setup recommendations.

- By default there is no HTTP authentication; read-only and mutation endpoints are both accessible to any LAN device.
- Enable `auth.enabled` with a strong password to protect mutation endpoints (config save, uploads, restart, WiFi change).
- `GET /api/config` redacts stored secrets (WiFi, MQTT, OTA, and auth passwords), but still exposes non-secret settings.
- HTTP traffic is plaintext, so credentials sent via config updates are visible to LAN observers. TLS is not supported on the ESP32 in this firmware.
- Command-line ArduinoOTA is disabled unless `ota.enabled` is `true` and `ota.password` is set.

Keep the device on a private network or isolated observatory VLAN. Do not expose port 80 or ArduinoOTA to the public internet.

---

## Updating via API

```bash
# Read current config
curl http://roofroof.local/api/config

# Partial update — only the fields you include are changed
curl -X POST http://roofroof.local/api/config \
  -H "Content-Type: application/json" \
  -d '{"deviceName": "observatory-roof", "ntp": {"server1": "time.google.com"}}'
```

Password fields returned by `GET /api/config` are masked as `********`. Sending a masked or empty password back to `POST /api/config` preserves the stored value; send `null` for a password field only when you intentionally want to clear it.

---

## Backup & Restore

**Settings → Device → Backup & restore** saves every setting to a file and puts it back, on the same
device or on a replacement.

### Back up

1. Optionally type a **Passphrase** (8 characters or more), twice.
2. Press **Back up settings**. The browser saves `roofroof-<name>-<date>.json`.

The file is readable JSON: a short header (format, firmware version, board, build, device name, when it
was made) and `settings`, exactly as `GET /api/config` shows them. Without a passphrase it holds **no
password, key or token** at all. With a passphrase, the secrets (WiFi, MQTT, ArduinoOTA and device
passwords, the Pushover keys, the ntfy token and the webhook authorization) are
added encrypted: AES-256-GCM under a key derived from the passphrase with PBKDF2-HMAC-SHA256 (20,000
iterations, random salt). The device makes the file; the secrets never leave it in clear text.

!!! warning "The passphrase can't be recovered"
    The passphrase isn't stored anywhere. If it's lost, the passwords and keys in that file are lost
    too; everything else in it still restores. Keep the file somewhere private all the same: it shows
    your network name, MQTT broker and alert settings.

### Restore

1. Press **Restore settings** and choose the file.
2. If the file has encrypted secrets, type its passphrase under **Passphrase for this file** and press
   **Preview**. Without it, this device keeps its own passwords and keys and everything else restores.
3. Check the preview, then press **Restore** (or **Cancel**: nothing changes until you press Restore).

The preview lists, per section, every setting that will change with its current and new value; each
password or key only as "Will change" or unchanged; settings **Not in the backup** (a backup from older
firmware: they keep their values); settings **Unknown to this firmware** (a backup from newer firmware:
they're ignored); and settings this board **Can't apply here**, with the reason. It says when restoring
restarts the device and warns before changes that can cut you off: another WiFi network (if the device
can't join it, use the [WiFi setup hotspot](../getting-started/first-setup.md)), a new device password
(the next page load asks for the backup's), a new local name, or a restart while an imaging app is
connected. Restore applies exactly what the preview showed, as one settings save with the usual checks.

A wrong passphrase, a damaged file, a file from newer firmware's backup format, or a file that isn't a
RoofRoof backup is refused with the reason, and nothing changes.

### Across boards and versions

A setting this device can't use isn't applied; the device keeps its own value and the preview lists it
under **Can't apply here**. So is Pushover switched on when its keys weren't restored.
A plain `GET /api/config` export still restores, without secrets.

### From a script

The same three steps are in the [REST API](../api/rest.md#post-apibackup), each needing the password
when protection is on:

```bash
# Back up (secrets encrypted with the passphrase)
curl -u admin:pass -X POST http://roofroof.local/api/backup \
  -H "Content-Type: application/json" -d '{"passphrase": "correct horse battery"}' -o backup.json

# Preview: nothing changes yet; note the token
jq -n --rawfile f backup.json '{backup: $f, passphrase: "correct horse battery"}' |
  curl -u admin:pass -X POST http://roofroof.local/api/backup/preview \
  -H "Content-Type: application/json" -d @-

# Apply exactly that preview
curl -u admin:pass -X POST http://roofroof.local/api/backup/apply \
  -H "Content-Type: application/json" -d '{"token": "<token from the preview>"}'
```
