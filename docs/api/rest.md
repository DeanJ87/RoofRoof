---
description: "RoofRoof REST API: conventions, errors, the password, limits and behaviour of every endpoint on the device."
---

# REST API

All endpoints are on port 80. Base URL: `http://<device-ip>/api`

The [API Reference](reference.md) lists every route with its parameters and response schemas, from
the machine-readable [`openapi.json`](openapi.json) (OpenAPI 3.1). Try any request in the
API explorer (in the [live demo](../live-demo.md)), where the demo answers it in your browser. This
page covers what the reference can't: behaviour, the password and limits.

---

## Conventions

- Keys are camelCase. Timestamps are Unix seconds (`timestamp`, `time.epoch`), `0` until the device's clock is set; `iso` strings are for display only. 
- **Success** is a 2xx status. Actions return `{"success": true, ...}`: 200 when done, 202 when queued.
- **Failure** is a 4xx or 5xx status with `{"error": "message"}`: 400 bad request, 401 unauthorised, 404 not found, 409 conflict, 500 device failure, 502 a broker didn't answer.
- Endpoints that change something need the password when [protection](../user-guide/security.md) is on.

!!! note "The same in every language"
    The UI language never changes the API. Numbers are JSON numbers with `.` as the decimal point and no thousands separators (`12.1`, never `"12,1"`). Field names and values such as `status`, `event` and `reason` are never translated, and timestamps are Unix seconds or ISO 8601. Text for people to read - alert `title` and `message`, `error` - is English and may be reworded between releases: key on the alert `event` and the other machine fields instead.

---

## Endpoints

### `GET /api/status`

Firmware, memory, flash, partitions, time, WiFi, MQTT, NTP, alerts, Alpaca, connections and the roof.

```bash
curl http://roofroof.local/api/status
```

```json
{
  "uptime": 3600,
  "configRevision": 3,
  "freeHeap": 128728,
  "firmware": { "name": "RoofRoof", "product": "roofroof", "version": "0.1.0", "buildDate": "Oct  8 2026", "buildTime": "12:00:00", "layout": "l2" },
  "time": { "epoch": 1791494565, "iso": "2026-10-08T22:22:45+0100", "timezone": "GMT0BST,M3.5.0/1,M10.5.0" },
  "wifi": { "connected": true, "ssid": "MyNetwork", "ip": "192.168.1.42", "rssi": -62, "mac": "AA:BB:CC:DD:EE:FF", "connectPending": false, "apMode": false, "hostname": "roofroof", "mdns": true, "ipv6": { "enabled": true, "addresses": [{ "address": "fe80::a00:27ff:fe4e:66a1", "scope": "link-local" }, { "address": "2a02:8010:abcd:1:a00:27ff:fe4e:66a1", "scope": "global" }] } },
  "alerts": { "armed": true, "mode": "any", "reason": "none", "since": null, "sinceAgeMs": null, "recentRevision": 7 },
  "alpaca": { "enabled": false, "clients": {} },
  "mqtt": { "enabled": true, "connected": true, "broker": "192.168.1.10", "port": 1883, "topic": "roofroof", "availabilityTopic": "roofroof/availability" },
  "connections": { "tcpLimit": 16, "liveUpdates": { "status": 1, "limitPerEndpoint": 3, "replaced": 0, "stalledClosed": 0 }, "alpacaRestoredAfterRestart": false },
  "roof": { "state": "closed", "positionKnown": true, "positionMm": 0, "positionPercent": 0, "target": null, "driving": false, "fromUnknown": false, "moveElapsedMs": 0, "moveTimeoutMs": 0, "hall": { "level": "low", "counts": 712 }, "supplyVolts": 12.46, "supplyLow": false, "minSupplyVolts": 11, "currentSense": false, "motorAmps": null, "maxCurrentAmps": 10, "parkSwitches": { "fitted": 1, "switch1": "closed", "switch2": "notFitted" }, "rainContact": "notFitted", "fault": null, "watchdogTrips": 0, "maxStepGapMs": 21 }
}
```

Shortened above: the document also carries heap and stack figures, `partitions` (running and next slot, web UI partition, NVS usage), `ntp` and the reset reason. The JSON Schema is `status.schema.json` and the [API Reference](reference.md) lists every field.

`firmware.product` is `roofroof` and `firmware.layout` is the partition layout (`l2`); an upload made for another product or layout is refused ([Updates](../user-guide/ota.md)).

`connections` shows who holds the device's TCP connections: the live-update clients on `/ws/status`, the most each endpoint takes, and how many were closed since the restart because a newer one replaced them (`replaced`) or they stopped reading (`stalledClosed`).

`alerts` is the [`/api/alerts/armed`](#get-apialertsarmed-post-apialertsarm-post-apialertsdisarm) object, plus `recentRevision`, which goes up whenever [`/api/alerts/recent`](#get-apialertsrecent) would answer differently. `configRevision` goes up whenever the settings are saved. Both only say *that* something changed, so a page fetches the details then; they restart from 0 after a restart. `alpaca.clients` is empty until an Alpaca device exists (a later spec).

`time.epoch` is Unix seconds like every other timestamp (`0` until the clock is set); `time.iso` is the device's local time with its UTC offset, for display.

`roof` is the roof as its control task last saw it ([Roof motion](../user-guide/roof-motion.md)):

| Field | Meaning |
|---|---|
| `state` | `unknown`, `closed`, `open`, `opening`, `closing`, `stopped` or `error` |
| `positionKnown`, `positionMm`, `positionPercent` | 0 is closed; `null` while the position is unknown |
| `target` | `open` or `closed` while opening or closing, else `null` |
| `driving` | the motor driver is on (false during the 500 ms dead time) |
| `fromUnknown` | the move started from an unknown position (slow, to the first magnet) |
| `moveElapsedMs`, `moveTimeoutMs` | time driven in this move and its limit |
| `hall.level`, `hall.counts` | `unsettled`, `low`, `between`, `high` or `rail`, and the raw 12-bit reading ([calibration](../hardware/calibration.md)) |
| `supplyVolts`, `supplyLow`, `minSupplyVolts` | the 12 V supply (`null` when unreadable), whether it is unreadable or below the minimum, and the minimum |
| `currentSense`, `motorAmps`, `maxCurrentAmps` | current sense on, the motor current (`null` while off or unreadable) and its limit |
| `parkSwitches` | `fitted` (0-2) and each switch: `closed`, `open`, `unreadable` or `notFitted` |
| `rainContact` | `notFitted`, `dry`, `raining` or `unreadable` |
| `fault` | `null`, or the latched fault: `kind`, `cause` (`logic`, `watchdog`, `parkInterlock`, `storedUnreadable`), `measured`, `limit` and `text` |
| `watchdogTrips`, `maxStepGapMs` | since the restart: how often the watchdog switched the driver off, and the longest gap between control steps |

`stackFree.roof` is the roof control task's unused stack in bytes.

---

### `GET /api/config`

Read the full device configuration. Password fields are returned as `********` when a stored password exists.

Every field, with its default, is in the [configuration reference](../user-guide/configuration.md#full-configuration-reference), including the `roof` section that sets how the roof moves ([Roof motion](../user-guide/roof-motion.md)).

```bash
curl http://roofroof.local/api/config
```

!!! warning "LAN-sensitive endpoint"
    Password fields are redacted in this response, but the endpoint is still unauthenticated and returns operational configuration. Treat `/api/config` as LAN-sensitive and do not expose the device to guest networks or the public internet.

---

### `POST /api/config`

Update configuration. Partial updates are supported — only the fields you send are changed.

```bash
curl -X POST http://roofroof.local/api/config \
  -H "Content-Type: application/json" \
  -d '{"deviceName": "observatory-roof", "ntp": {"server1": "time.google.com"}}'
```

Masked or empty password fields preserve the existing stored password. Send `null` for `wifi.password`, `mqtt.password`, or `ota.password` only when you intentionally want to clear that password.

Every value must have the setting's type: text for names and addresses, `true`/`false` for switches, numbers for numbers. A value of the wrong type, `null` included (other than `null` to clear a password), is rejected with `400` and an error naming the setting, e.g. `{"error": "Settings: wifi.ssid must be a string"}`; it is never replaced by a default. A setting you leave out keeps its current value.

While the device has a WiFi network saved, `wifi.ssid` can't be set to `""`: the save is rejected with `"WiFi network can't be blank; use WiFi setup to change networks"`, so a settings save never takes the device off its network. Join a different network with [`POST /api/wifi/connect`](#post-apiwificonnect) (or pick it in Settings → Network); there is no endpoint that forgets the network.

Successful saves return:

```json
{ "success": true }
```

!!! note
    `POST` is the primary method. The firmware also accepts `PUT` for compatibility with full-resource settings clients.

While the roof is opening or closing, a save that changes any `roof` or `park` setting is refused with `409` and `{"error": "The roof is moving: roof and park settings can't change until it stops"}`. Other settings save normally. Settings saved while the roof is stopped part-way make its position unknown (the estimate used the old ones).

A setting whose dependency is off is saved as sent and reported inactive by `GET /api/settings/effective`; it isn't rejected. Only combinations that can never work are refused, with the reason in `error` (see [Settings dependencies](../reference/settings-dependencies.md)).

---

### `POST /api/backup`

The settings backup file ([format](../user-guide/configuration.md#backup-restore)): a header and every setting as `GET /api/config` shows it. With `{"passphrase": "..."}` (8 characters or more) the passwords, keys and tokens are added encrypted (AES-256-GCM, PBKDF2-HMAC-SHA256, 20,000 iterations); without one the file holds no secret. An empty body is fine. Needs the password when protection is on.

```bash
curl -X POST http://roofroof.local/api/backup -H "Content-Type: application/json" \
  -d '{"passphrase": "correct horse battery"}' -o backup.json
```

The key derivation takes a second or two, in a background task: the web server keeps answering. One backup or preview runs at a time (`409`, `"code": "busy"`). Refusals add a `code`: `passphrase-short` (400), `too-large` (413, bodies over 20,480 bytes).

---

### `POST /api/backup/preview`

What restoring a backup would save. Nothing is applied.

```json
{ "backup": "<the backup file's text, or its JSON>", "passphrase": "correct horse battery" }
```

```json
{
  "token": "4f1c0d7e9a2b3c4d5e6f708192a3b4c5",
  "backup": { "format": 1, "firmware": "0.1.0", "board": "esp32", "build": "standard", "deviceName": "observatory-roof", "createdAt": "2026-10-18T21:04:05Z" },
  "settings": { "deviceName": "observatory-roof", "wifi": { "ssid": "Observatory", "password": "********" } },
  "secrets": { "state": "restored", "changed": ["wifi.password"], "unchanged": ["mqtt.password"] },
  "notApplicable": [],
  "restart": ["wifi"]
}
```

`settings` is what applying saves, masked like `GET /api/config` (shortened above). `secrets.state` is `restored` (passphrase right), `kept` (the file has secrets, no passphrase given: this device keeps its own) or `none`. `notApplicable` lists settings this board can't use (`pin`, `pushover-keys`); they keep this device's values. `restart` says why applying restarts the device (empty: it doesn't). The web UI works out the per-setting differences from `settings`.

Refused with `400` and a `code`: `not-backup`, `damaged`, `newer-format`, `wrong-passphrase` (also when the encrypted part was changed), or `invalid` with the same message a settings save gives. A new preview replaces the previous one.

---

### `POST /api/backup/apply`

Saves exactly what the preview with this token showed, as one settings save.

```bash
curl -X POST http://roofroof.local/api/backup/apply -H "Content-Type: application/json" \
  -d '{"token": "4f1c0d7e9a2b3c4d5e6f708192a3b4c5"}'
```

```json
{ "success": true, "restarting": true }
```

With `restarting`, the device restarts about 2 s later. A restore that changes `roof` or `park` settings while the roof is opening or closing is refused with `409`, as a settings save is. `409` with `"code": "stale"` when the token was already used or replaced, is more than 10 minutes old, or the settings changed after the preview: preview again.

---

### `GET /api/settings/effective`

Whether each setting that depends on another setting, on hardware or on the network is actually in effect. Settings are reported as saved; this says what the device does with them. Read-only, no secrets.

```json
{
  "facts": { "wifiConnected": true, "mqttConnected": false, "clockSet": true },
  "settings": [
    { "id": "D-01", "setting": "alerts.mqtt.enabled", "state": "inactive", "reason": "mqtt-off", "text": "MQTT is off", "fix": "network#mqtt" },
    { "id": "D-28", "setting": "ntp.enabled", "state": "active" }
  ]
}
```

| Field | Meaning |
|---|---|
| `facts` | What the device knows at run time: network, broker and clock |
| `id` | [Catalogue](../reference/settings-dependencies.md) entry that decides the state (the first unmet one when inactive) |
| `setting` | Path of the setting in `/api/config` |
| `state` | `off` (switched off), `active`, or `inactive` (on, but something it needs is missing) |
| `reason`, `text`, `fix` | Why it's inactive and where it's fixed (`tab#section` in Settings, or `restart`) |

---

### `GET /api/wifi/scan`

Scan for nearby WiFi networks (needs the password when [protection](../user-guide/security.md) is on, like `/api/wifi/connect`). The scan runs in the background: the first call starts it and returns `202` with `"scanning": true`; call again (about once a second) until `"scanning": false` and the list arrives.

```bash
curl http://roofroof.local/api/wifi/scan
```

```json
{
  "success": true,
  "scanning": false,
  "networks": [
    { "ssid": "MyNetwork", "rssi": -55, "encryption": "secured" },
    { "ssid": "Guest", "rssi": -80, "encryption": "open" }
  ]
}
```

---

### `POST /api/wifi/connect`

Join a network - what the WiFi setup screen (`/wifi`) uses. Returns `202` at once; the attempt takes up to 10 seconds. Poll [`GET /api/status`](#get-apistatus): `wifi.connectPending` is `true` while it's trying; afterwards `wifi.connected` and `wifi.ssid` tell you whether it worked. On success the credentials are saved and the device restarts onto the network 15 seconds later.

```bash
curl -X POST http://roofroof.local/api/wifi/connect \
  -H "Content-Type: application/json" \
  -d '{"ssid": "MyNetwork", "password": "hunter2"}'
```

```json
{ "success": true, "pending": true, "message": "Connection started" }
```

`400 {"error": "Missing SSID or password"}` if either field is absent (send `""` for an open network).

---

### `POST /api/restart`

Restart the device.

```bash
curl -X POST http://roofroof.local/api/restart
```

---

## Roof

Each of these needs the password when protection is on, and counts as a person's command (a person may move a roof whose position is unknown, slowly, to the first magnet). The answer comes after the command has reached the motor driver.

```json
{ "success": true, "state": "opening" }
```

A refusal is `409` with the reason and a text that states the measured value and the limit, or the input that failed:

```json
{ "success": false, "state": "open", "reason": "mountNotParked", "error": "Mount not parked: park switch 1 not closed" }
```

### `POST /api/roof/open`

Open. Refused (`weatherNotSafe`) unless the weather verdict is safe, and by the motion logic: `faultLatched`, `hallNotSettled`, `supplyLow`, `supplyUnreadable`, `currentUnreadable`, `settingsInvalid`. The motor starts after the 500 ms dead time and stops on the open magnet. An open in progress stops (no fault) as soon as the weather is no longer safe.

```bash
curl -X POST http://roofroof.local/api/roof/open
```

### `POST /api/roof/close`

Close. Refused (`mountNotParked`) unless the mount is confirmed parked: N.I.N.A. reports it at park and every fitted park switch reads closed, as the [safety inputs](../user-guide/safety-inputs.md) decide. During the close the park is checked every 20 ms; if it is lost the motor stops with a `parkInterlock` fault. The weather never blocks a close.

### `POST /api/roof/stop`

Stop at once, keeping the position estimate (`stopped`). Always accepted.

### `POST /api/roof/clear-fault`

Clear a latched fault after fixing its cause. The fault is removed from storage too; the position is known again only at a magnet. Accepted with no fault.

---

### `POST /api/update`

OTA firmware update. Send a raw `.bin` file as `multipart/form-data`.

```bash
curl -X POST http://roofroof.local/api/update \
  -F "firmware=@roofroof-l2-firmware-v0.1.0.bin"
```

Returns 200 `{"success": true}` and reboots; 400 `{"error": "..."}` when the file isn't for this device: another product, or another partition layout (nothing was changed); or 500 `{"error": "..."}` (for example "Could not activate partition") and keeps the running firmware.

---

### `POST /api/update/fs`

OTA filesystem update. Send a LittleFS image as `multipart/form-data`.

```bash
curl -X POST http://roofroof.local/api/update/fs \
  -F "filesystem=@roofroof-l2-littlefs-v0.1.0.bin"
```

Use this endpoint for web UI assets only. It does not update firmware and does not erase NVS configuration. The image must fill the web UI partition exactly; one for a different layout gets 400 before anything is erased. Responses are as for `/api/update`.

Both upload endpoints need the password when protection is on. Keep RoofRoof on a trusted network and don't port-forward it.

---

### `POST /api/mqtt/test`

Tries to connect to a broker with the given details, without saving them: `{"broker": "192.168.1.10", "port": 1883, "username": "", "password": "", "clientId": "RoofRoof-Test"}`. Returns 200 `{"success": true, "message": "Connection successful"}`, or 502 `{"error": "Connection timeout", "state": -4}`.

A `password` of `********` (what `GET /api/config` returns for a saved password) or an empty one means "the saved password", so testing an unchanged form works. The saved password is only used when `broker`, `port` and `username` match the saved settings; a masked password for any other broker returns 400 `{"error": "Type the MQTT password to test a broker other than the saved one"}`, so a saved secret is never sent to another host.

---

## Alerts

See [Alerts](../user-guide/alerts.md) for setup.

### `POST /api/alerts/test?channel=<mqtt|pushover|ntfy|webhook|all>`

Queues a test notification on the given (saved and enabled) channel(s). Returns `202 {"success":true}`; delivery happens in the background - check `/api/alerts/recent` for the result. `400` if the channel is unknown or not enabled. A channel that is switched on but can't deliver right now (MQTT off, no WiFi) is recorded as `skipped` with the reason, never `failed`. Requires HTTP auth when enabled.

Add `event=<client_lost|client_back|client_disconnected>&level=<1-4>&sound=<pushover sound>` to send a sample of that event (title "Test: ...") at that level and sound instead. `title` and `message` (up to 80 / 240 characters) try out custom wording with `{variables}`.

### `GET /api/alerts/armed`, `POST /api/alerts/arm`, `POST /api/alerts/disarm`

Whether alerts are being sent ([When to send](../user-guide/alerts.md#when-to-send)), for automations:

```json
{"armed": false, "mode": "whileConnected", "reason": "client-disconnected", "since": "2026-10-08T05:42:10Z", "sinceAgeMs": 734000}
```

- `armed`: `true` sending, `false` paused.
- `mode`: `any` or `whileConnected` (the `sendMode` setting).
- `reason`: why it's sending or paused - `user-ui` (the web UI's Pause/Resume), `user-rest` (`arm`/`disarm`), `user-mqtt` (`<base>/alerts/armed/set`), `client-connected`, `client-disconnected`, `waiting-for-client` (while-connected mode with nothing connected), `migrated` (paused before an update that added reasons) or `none`.
- `since`: when it changed, ISO 8601 UTC; `null` without a clock. `sinceAgeMs`: milliseconds since, on the uptime clock; `null` if it changed before the last restart.

`arm` (resume) / `disarm` (pause) return 202 `{"success": true, "armed": true|false}`, take effect immediately, and the state survives restarts; while paused nothing is sent and . The web UI adds `?source=ui`. POSTs require HTTP auth when enabled. `/api/alerts/recent` also carries `armed`, and `/api/status` carries the same object as `alerts`.

### `POST /api/alerts/clear`

Empties the recent-alerts list. Requires HTTP auth when enabled.

### `GET /api/alerts/recent`

Whether alerts are on, and the last 20 alerts since boot, newest first:

```json
{"enabled":true,"alerts":[{"id":2,"event":"client_lost","title":"Imaging app stopped checking","message":"No request to the device for 2 min.","level":"urgent","ageSeconds":42,"timestamp":1759500000,
  "channels":{"pushover":{"status":"sent","detail":"HTTP 200"},"mqtt":{"status":"failed","detail":"MQTT not connected"}}}]}
```

`status` is `pending`, `sent`, `failed` or `skipped`.

---

## ASCOM Alpaca API

RoofRoof is an ASCOM Alpaca server - see [ASCOM Alpaca](../user-guide/alpaca.md). No device is served yet (the roof's Dome device *comes in a later spec*). The HTTP surface (all under the same port-80 server as the rest of the API, response envelope per the [ASCOM Alpaca API spec](https://ascom-standards.org/api/)):

| Endpoint | Purpose |
|----------|---------|
| `GET /management/apiversions` | Supported Alpaca API versions |
| `GET /management/v1/description` | Server description: `Manufacturer` `RoofRoof`, `ServerName` the device name |
| `GET /management/v1/configureddevices` | The configured devices: an empty list for now |
| `GET /setup` | Redirects to **Settings → Network → ASCOM Alpaca** in the web UI |
| `/api/v1/...` | Any device request: HTTP `400` "Unknown device" |

A UDP listener on port `32227` answers Alpaca discovery broadcasts (`alpacadiscovery1` → `{"AlpacaPort":80}`) whenever Alpaca is enabled in settings - this requires a device restart to start/stop.
