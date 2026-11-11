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
  "alpaca": { "enabled": true, "clients": { "dome": { "connected": true, "watching": true, "silent": false, "lastCheckedAgeMs": 1200, "clientId": 4711 } } },
  "mqtt": { "enabled": true, "connected": true, "broker": "192.168.1.10", "port": 1883, "topic": "roofroof", "availabilityTopic": "roofroof/availability" },
  "connections": { "tcpLimit": 16, "liveUpdates": { "status": 1, "limitPerEndpoint": 3, "replaced": 0, "stalledClosed": 0 }, "alpacaRestoredAfterRestart": false },
  "roof": { "state": "closed", "positionKnown": true, "positionMm": 0, "positionPercent": 0, "target": null, "driving": false, "fromUnknown": false, "moveElapsedMs": 0, "moveTimeoutMs": 0, "hall": { "level": "low", "counts": 712 }, "supplyVolts": 12.46, "supplyLow": false, "minSupplyVolts": 11, "currentSense": false, "motorAmps": null, "maxCurrentAmps": 10, "parkSwitches": { "fitted": 1, "switch1": "closed", "switch2": "notFitted" }, "rainContact": "notFitted", "closePending": false, "calibrating": false, "closePhase": "idle", "weather": "safe", "weatherText": "safe", "parkConfirmed": true, "parkText": "parked", "daylight": { "mode": "alert", "state": "night", "sunAltitudeDeg": -14.2, "limitDeg": 0, "text": "Sun -14.2° < 0.0°", "closes": false }, "fault": null, "watchdogTrips": 0, "maxStepGapMs": 21, "maxWatchdogBeatAgeMs": 12, "simulation": "off" }
}
```

Shortened above: the document also carries heap and stack figures, `partitions` (running and next slot, web UI partition, NVS usage), `ntp` and the reset reason. The JSON Schema is `status.schema.json` and the [API Reference](reference.md) lists every field.

`firmware.product` is `roofroof` and `firmware.layout` is the partition layout (`l2`); an upload made for another product or layout is refused ([Updates](../user-guide/ota.md)).

`connections` shows who holds the device's TCP connections: the live-update clients on `/ws/status`, the most each endpoint takes, and how many were closed since the restart because a newer one replaced them (`replaced`) or they stopped reading (`stalledClosed`).

`alerts` is the [`/api/alerts/armed`](#get-apialertsarmed-post-apialertsarm-post-apialertsdisarm) object, plus `recentRevision`, which goes up whenever [`/api/alerts/recent`](#get-apialertsrecent) would answer differently. `configRevision` goes up whenever the settings are saved. Both only say *that* something changed, so a page fetches the details then; they restart from 0 after a restart. `alpaca.clients` has one entry per Alpaca device, keyed by its type: `dome`.

`time.epoch` is Unix seconds like every other timestamp (`0` until the clock is set); `time.iso` is the device's local time with its UTC offset, for display.

`roof` is the roof as its control task last saw it (the same object is published on MQTT as `<base>/state`, [MQTT](../user-guide/mqtt.md#the-roof-document)) ([Roof motion](../user-guide/roof-motion.md)):

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
| `closePending` | a close was accepted and waits for an upload or MQTT connect to finish |
| `calibrating` | a [calibration run](../user-guide/calibration-run.md) is under way, pauses at each end included; open and close are refused (`calibrating`) and the Stop bar shows |
| `closePhase` | the [automatic close](../user-guide/close-on-unsafe.md)'s phase: `idle`, `waitPark`, `closing`, `retryWait`, `heldByPerson`, `cannotMove` or `halted` |
| `weather`, `weatherText` | the weather verdict the roof's command checks use now (`safe`, `unsafe` or `unknown`; unknown and unsafe when the main loop hasn't handed it over for 5 s) and its reason, at most 200 bytes |
| `parkConfirmed`, `parkText` | whether a close would pass the park check now (N.I.N.A. and every fitted switch), and why, at most 200 bytes |
| `daylight` | the [daylight failsafe](../user-guide/close-on-unsafe.md#daylight-failsafe): `mode` (`off`, `alert` or `close`), `state` (`off`, `unknown`, `night` or `day`), `sunAltitudeDeg` (`null` while off or unknown), `limitDeg` (the threshold), `text` ("Sun 3.2° ≥ 0.0°", "Sun -4.1° < 0.0°", "Location not set", "Clock not set" or "Off") and `closes` (daylight wants the roof closed now: mode `close` and state `day`) |
| `fault` | `null`, or the latched fault: `kind`, `cause` (`logic`, `watchdog`, `watchdogStopped`, `parkInterlock`, `storedUnreadable`, `benchSupply`), `measured`, `limit` and `text` |
| `simulation` | `off` on a real roof; `bench` for the bench-mode firmware (a simulated roof, the motor never driven; see [Bench mode](../user-guide/bench-mode.md)); `demo` in the online demo |
| `watchdogTrips`, `maxStepGapMs`, `maxWatchdogBeatAgeMs` | since the restart: how often the watchdog switched the driver off, the longest gap between control steps, and the oldest watchdog heartbeat a step saw (the driver needs it at most 50 ms old) |

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

While the roof is opening or closing, every settings save is refused with `409` and `{"error": "The roof is moving: stop it first, then try again"}`: writing the flash pauses the processor's code cache, and with it the roof's control task and watchdog. A save is also refused while a close is waiting to start (`"A close is waiting to start: try again once the roof has closed"`), and while it is being written an open is refused (`busy`) and a close waits for it. Restores, uploads, WiFi changes and restarts are refused the same way; restarts, uploads, WiFi changes and scans also while the automatic close is waiting for the park or closing (`"The automatic close is under way: try again once the roof has closed or the weather is safe"`). The full table is in [Roof motion](../user-guide/roof-motion.md#while-the-roof-moves). Settings saved while the roof is stopped part-way make its position unknown (the estimate used the old ones).

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

With `restarting`, the device restarts about 2 s later. While the roof is opening or closing or a close is waiting to start, a restore is refused with `409`, as a settings save is; one that would restart also while the automatic close is waiting for the park or closing. `409` with `"code": "stale"` when the token was already used or replaced, is more than 10 minutes old, or the settings changed after the preview: preview again.

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

`400 {"error": "Missing SSID or password"}` if either field is absent (send `""` for an open network). `409` with the reason while the roof is opening or closing, a close is waiting to start or the automatic close is waiting for the park or closing: trying a network drops the current one, and with it N.I.N.A. A scan (`GET /api/wifi/scan`) is refused the same way. While either runs (at most 15 s), an open is refused (`busy`) and a close waits for it.

---

### `POST /api/restart`

Restart the device. Refused with `409` and the reason while the roof is opening or closing, a close is waiting to start, or the automatic close is waiting for the park or closing.

```bash
curl -X POST http://roofroof.local/api/restart
```

---

## Roof

Each of these needs the password when protection is on and the header `X-RoofRoof: 1` (without it: `403`), and counts as a person's command (a person may move a roof whose position is unknown, slowly, to the first magnet). The header stops another web page open in your browser from moving the roof with a cross-site request: a plain form or simple cross-origin request can't send it. The answer comes after the command has reached the motor driver.

```bash
curl -X POST -H "X-RoofRoof: 1" http://roofroof.local/api/roof/open
```

```json
{ "success": true, "state": "opening" }
```

A refusal is `409` with the reason and a text that states the measured value and the limit, or the input that failed:

```json
{ "success": false, "state": "open", "reason": "mountNotParked", "error": "Mount not parked: park switch 1 not closed" }
```

### `POST /api/roof/open`

Open. Refused (`weatherNotSafe`) unless the weather verdict is safe, (`daylight`, "Daylight: Sun 3.2° ≥ 0.0°") while `roof.daylight.closes` is true (the weather reason comes first when both apply), (`busy`) while an upload, command-line OTA or a settings save is writing the flash, an MQTT connect is under way or a restart is scheduled, (`hotspot`) while the setup hotspot (no password) is on, and by the motion logic: `faultLatched`, `hallNotSettled`, `supplyLow`, `supplyUnreadable`, `currentUnreadable`, `settingsInvalid`. The motor starts after the 500 ms dead time and stops on the open magnet. An open in progress stops (no fault) as soon as the weather is no longer safe, or `roof.daylight.closes` becomes true.


### `POST /api/roof/close`

Close. Refused (`mountNotParked`) unless the mount is confirmed parked: N.I.N.A. reports it at park and every fitted park switch reads closed, as the [safety inputs](../user-guide/safety-inputs.md) decide. During the close the park is checked every 20 ms; if it is lost the motor stops with a `parkInterlock` fault. The weather never blocks a close.

A close always wins, as long as it would run (past the park check and the motion checks, and not already closed or closing): it cancels a scheduled restart (except the one that remounts a rewritten web UI) and stops an upload in progress (that upload fails with `409` "Upload stopped: the roof is closing"). Once a restart has passed its last check, open and close are refused (`restarting`, "The device is restarting") for the moment before the reset. If an upload, a settings save, a WiFi change or scan, or an MQTT connect is still finishing, the close is accepted as waiting and starts as soon as it is done, within a few seconds (`roof.closePending` in the status); stop or open cancels it:

```json
{ "success": true, "state": "open", "pending": true, "message": "Close starts once this has stopped: An upload is being written" }
```

### `POST /api/roof/stop`

Stop at once, keeping the position estimate (`stopped`). Always accepted.

### `POST /api/roof/clear-fault`

Clear a latched fault after fixing its cause. The fault is removed from storage too; the position is known again only at a magnet. Accepted with no fault.

### `GET /api/roof/inputs`

The weather sources, the park and N.I.N.A. as the device last evaluated them (once a second): the weather verdict with each SQMeter source's state and reason, the rain contact, the park result, and N.I.N.A.'s last mount reading. The [setup wizard](../user-guide/setup-wizard.md)'s **Test** reads it. Open (no password), like `/api/status`: it returns readings and the device's texts, no address or secret, and sends nothing to SQMeter or N.I.N.A. In the rare moment the device is updating the readings for longer than 50 ms it answers `503` (`{"error": "Inputs not available yet: try again"}`).

```json
{
  "weather": {
    "verdict": "safe",
    "text": "safe",
    "http": { "enabled": true, "state": "good", "generation": 1, "ageMs": 2100, "limitMs": 30000, "safe": true, "flags": 0, "text": "" },
    "mqtt": { "enabled": false, "state": "off", "generation": 0, "ageMs": 0, "limitMs": 150000, "safe": false, "flags": 0, "text": "" },
    "rainContact": "notFitted"
  },
  "park": { "parked": true, "text": "parked" },
  "nina": { "state": "good", "generation": 1, "connected": true, "atPark": true, "slewing": false, "ageMs": 900, "text": "" }
}
```

| Field | |
|---|---|
| `weather.verdict`, `weather.text` | `safe`, `unsafe` or `unknown`, and the same text as `roof.weatherText` in the status |
| `weather.http`, `weather.mqtt` | Each SQMeter source: `state` (`off`, `waiting`, `good`, `stale`, `unreachable`, `unreadable`, `offline`), `ageMs` since its last good reply, the stale `limitMs`, SQMeter's `safe` and `flags` (its `reasonFlags`) from that reply, and the reason `text` ("" when off, or good and safe) |
| `park.parked`, `park.text` | The park rule's answer, as `roof.parkConfirmed` and `roof.parkText` |
| `nina.state` | `notConfigured`, `waiting`, `good`, `unreachable`, `unreadable` or `apiError`; `connected`, `atPark` and `slewing` only mean something when it is `good`; `ageMs` since the reading; `text` the failure |
| `…generation` | Goes up when that source's settings change (its record starts again), so a client can tell a reading of the new settings from an old one |

---

### `POST /api/roof/calibration/start`

Starts an attended [calibration run](../user-guide/calibration-run.md) (spec 016). Login and the `X-RoofRoof: 1` header, like every roof command. Body:

```json
{ "attended": true, "at": "closed", "travelMm": 1850, "topPass": false }
```

`attended` must be `true` (the person is at the roof and can see it); `at` is where the roof is now (`closed`, `open` or `partWay`), checked against the Hall reading; `travelMm` the travel between the magnets' trigger points, measured with a tape (100-20000); `topPass` adds an open and a close at the top duty. `200 {"success": true, "state": "running"}`; `400` for a body of another shape; `409` with `reason` and `error` when refused: `notAttended`, `calibrating`, `closeSequence` (the automatic close isn't idle), `faultLatched`, `moving`, `closeWaiting`, `settingsPending`, `weatherNotSafe`, `daylight`, `mountNotParked`, `busy`, `hotspot`, `restarting`, `invalid`, `hallDisagrees`, `notStarted`, or the motion logic's refusals. Nothing is saved by the run.

### `POST /api/roof/calibration/confirm`

After the slow open the run waits (`leg.phase` `confirm`, at most 5 minutes) for the person at the roof to say whether the roof is now fully open: `{"atOpenEnd": true}` carries on with the slow close, `{"atOpenEnd": false}` ends the run (`wrongEnd`, nothing proposed). The run tells the ends apart by the direction it drives, so only a person can catch a motor that runs the other way. Login and `X-RoofRoof: 1`; `409` with `notWaiting` when the run isn't waiting.

### `POST /api/roof/calibration/abort`

Stops the roof as a person's stop and ends any run (no proposal, settings unchanged). Login and `X-RoofRoof: 1`. Answers like `POST /api/roof/stop`. A stop from anywhere else ends a run too.

### `GET /api/roof/calibration`

The last run since start-up. Open, like `/api/status`: readings and derived values only. The run is kept in memory only; after a restart it reads `idle`.

```json
{
  "state": "done",
  "active": false,
  "request": { "at": "closed", "travelMm": 1850, "topPass": false, "limitSeconds": 180 },
  "leg": null,
  "ending": { "reason": "done", "text": "Calibration finished" },
  "legs": [
    { "name": "slowOpen", "target": "open", "dutyPercent": 40, "drivenMs": 112800, "level": "high",
      "speedMmPerSecond": 41.0, "ampsMean": null, "ampsPeak": null, "supplyMinVolts": 12.3 }
  ],
  "hall": {
    "between": { "mean": 2050, "min": 2041, "max": 2060, "noise": 4.2 },
    "closed": { "mean": 702, "min": 699, "max": 705, "level": "low" },
    "open": { "mean": 3398, "min": 3395, "max": 3401, "level": "high" }
  },
  "currentSense": false,
  "maxCurrentAmps": 10,
  "proposal": {
    "travelMm": { "old": 1850, "new": 1850 },
    "speedMmPerSecond": { "old": 38, "new": 41 },
    "timeoutMarginSeconds": { "old": 15, "new": 10 },
    "hallHighEnd": { "old": "open", "new": "open" },
    "hallHighCounts": { "old": 3000, "new": 3000 },
    "hallLowCounts": { "old": 1100, "new": 1100 },
    "speedProblem": null,
    "hallProblem": null,
    "warnings": []
  }
}
```

| Field | |
|---|---|
| `state` | `idle` (no run since start-up), `running`, `done`, `failed` (a fault, a refused leg, blocked for 15 s) or `stopped` (a stop, the weather, the park, the automatic close) |
| `leg` | While running: `index` of `count`, `name` (`toClosed`, `slowOpen`, `slowClose`, `topOpen`, `topClose`), `phase` (`settle` at an end, `wait` for a blocker, `confirm` for the person's answer, `drive`), `elapsedMs` and the leg's `limitMs` |
| `ending` | `reason` (`done`, `stopped`, `weather`, `park`, `closeSequence`, `fault`, `refused`, `busy`, `wrongEnd`, `unconfirmed`) and its text |
| `legs[]` | Each measured leg: time driven, duty, the level it ended on, the speed at 100 % duty it implies, and with current sense the mean and peak running current; the lowest supply |
| `hall` | The in-between reading (middle half of each leg) with its noise (standard deviation), and the reading at rest at each end |
| `proposal` | Only when `done`: the value at the start (`old`) and the proposed one (`new`); `null` where withheld, with `speedProblem` (`legsDisagree`, `legTooShort`, `outOfRange`) or `hallProblem` (`samePole`, `weakEnd`, `noRoom`); `warnings[]` (`directionsDiffer`, `currentNearLimit`) |

The status document's `roof.calibrating` is `true` for the whole run, pauses included.

---

## Diagnostics

The [advanced diagnostics](../user-guide/diagnostics.md) routes. They are open (readings only, no password), like `/api/status`, except clearing the log. With `diag.enabled` off, each read answers `200 {"enabled": false}` (a move by id answers `404`) and records nothing. A value the device can't read is `null`.

### `GET /api/diag`

The live document: the roof, the control task, the main loop, memory and WiFi at this moment. The same document is streamed on [`/ws/diag`](websocket.md#diagnostics-stream) and published on `<base>/diag` ([MQTT](../user-guide/mqtt.md#diagnostics)).

```json
{
  "enabled": true,
  "log": "ready",
  "simulation": "off",
  "roof": {
    "state": "opening", "target": "open", "phase": "cruise",
    "driving": true, "direction": "forward", "duty": 255, "dutyPercent": 100,
    "en": true, "rpwm": 255, "lpwm": 0,
    "supplyVolts": 12.31, "restVolts": 12.62, "sagVolts": 0.31,
    "currentSense": true, "motorAmps": 3.42, "powerWatts": 42.1,
    "hallCounts": 2050, "hall": "between",
    "positionKnown": true, "positionMm": 812.5,
    "elapsedMs": 21400, "remainingMs": 27300, "timeoutMs": 63700,
    "driveBudgetUsedMs": 20900, "driveBudgetMs": 177000
  },
  "control": {
    "periodMs": 20, "stepUs": 412, "maxStepUs": 980, "maxGapMs": 21,
    "watchdogLimitMs": 100, "watchdogMarginMs": 79, "maxBeatAgeMs": 11, "watchdogTrips": 0
  },
  "loop": { "passMs": 3, "maxPassMs": 41 },
  "memory": {
    "freeHeap": 101234, "minFreeHeap": 84512, "largestBlock": 65524,
    "stackFree": { "control": 3120, "loop": 6044, "network": 4380 }
  },
  "wifi": { "rssi": -61 },
  "uptimeS": 12345,
  "resetReason": "power-on",
  "move": { "id": 12, "active": true }
}
```

| Field | |
|---|---|
| `log` | `ready`, or `unavailable` when the log's memory couldn't be allocated (then `move` is `null`) |
| `simulation` | `off`, `bench` or `demo`, as in the status |
| `roof.phase` | `idle`, `deadTime`, `slow`, `softStart`, `cruise`, `slowing` or `creep` |
| `roof.direction` | `off`, `forward` or `reverse`: the driver's command. Which one is open is the `openDirection` setting |
| `roof.restVolts`, `roof.sagVolts` | The supply averaged over a full second with the motor off before the move; and that minus the supply now while driven, else `null` |
| `roof.powerWatts` | Supply times current; `null` without either |
| `roof.remainingMs` | `null` unless the roof moves with its position known |
| `roof.driveBudgetUsedMs` | Toward the move's target while moving; at rest the larger of the two ends |
| `control.watchdogMarginMs` | The watchdog limit minus the longest gap between steps since start |
| `move` | The move in progress or the last one; `null` before the first |

In the [demo](../live-demo.md) `control.stepUs`, `control.maxStepUs`, `loop`, `memory`, `wifi.rssi` and `resetReason` are `null`.

### `GET /api/diag/moves`

The move summaries, newest first: up to `maxSummaries` (32).

```json
{
  "enabled": true,
  "keep": false,
  "maxMoves": 5,
  "maxSummaries": 32,
  "moves": [
    {
      "id": 12, "active": false, "kept": false, "profile": true,
      "startMs": 1203400, "endMs": 1253900, "startEpoch": 1762898400, "endEpoch": 1762898450,
      "origin": "person", "target": "open", "endState": "open", "result": "arrived",
      "fault": null,
      "durationMs": 50500, "drivenMs": 49900, "fromUnknown": false,
      "peakAmps": 6.10, "averageAmps": 3.30, "stallAmps": null,
      "restVolts": 12.62, "minVolts": 12.01, "sagVolts": 0.61,
      "hallStart": 1020, "hallEnd": 3120
    }
  ]
}
```

| Field | |
|---|---|
| `keep` | `diag.keepSummaries` |
| `origin` | `person`, `automatic`, `client` (an Alpaca client) or `mqtt` |
| `result` | `arrived`, `stopped` or `fault`. For `fault`, `fault` is `{"kind", "measured", "limit"}` as in the status, else `null` |
| `stallAmps` | Only for a fault while driving; else `null` |
| `kept` | Loaded from the kept file: `startMs` and `endMs` are `null` and `profile` is `false` |
| `profile` | The move is still held with its profile, so `GET /api/diag/moves/{id}` answers |
| `startEpoch`, `endEpoch` | Unix seconds; `0` when the clock wasn't set |

### `GET /api/diag/moves/{id}`

One move's summary and profile. `404 {"error": "..."}` when the move isn't held (only the last 5 are, and none after a restart).

```json
{
  "enabled": true,
  "move": { "id": 12, "result": "arrived" },
  "intervalMs": 500,
  "columns": ["tMs", "ampsAvg", "ampsMax", "voltsMin", "dutyPercent"],
  "points": [[0, 1.20, 4.80, 12.55, 40], [500, 2.95, 3.10, 12.20, 62]]
}
```

`move` is the whole summary, shortened here. Point *i* covers `i × intervalMs` up to the next point from the move's start; the interval starts at 250 ms and doubles each time the 120 points are full, so a move always fits.

`GET /api/diag/moves/12?format=csv` answers `text/csv` as a download named `roofroof-move-12.csv`, with the header `t_ms,amps_avg,amps_max,volts_min,duty_percent` and one row per point. A value that can't be read is an empty field.

### `POST /api/diag/moves/clear`

Empty the move log and the summary list. Needs the password when protection is on and the header `X-RoofRoof: 1` (without it: `403`).

```bash
curl -X POST -H "X-RoofRoof: 1" http://roofroof.local/api/diag/moves/clear
```

Answers `200 {"success": true}`. `409 {"error": "..."}` while the roof moves or a close is waiting. The kept file is emptied afterwards (removed while "Keep move summaries" is off), by the main loop, at its next chance to write flash.

---

### `POST /api/update`

OTA firmware update. Send a raw `.bin` file as `multipart/form-data`.

```bash
curl -X POST http://roofroof.local/api/update \
  -F "firmware=@roofroof-l2-firmware-v0.1.0.bin"
```

Returns 200 `{"success": true}` and reboots; 400 `{"error": "..."}` when the file isn't for this device: another product, or another partition layout (nothing was changed); or 500 `{"error": "..."}` (for example "Could not activate partition") and keeps the running firmware. `409` with the reason, before anything is written, while the roof is opening or closing, a close is waiting to start or the automatic close is waiting for the park or closing; the restart after a successful upload waits for the roof and the close.

---

### `POST /api/update/fs`

OTA filesystem update. Send a LittleFS image as `multipart/form-data`.

```bash
curl -X POST http://roofroof.local/api/update/fs \
  -F "filesystem=@roofroof-l2-littlefs-v0.1.0.bin"
```

Use this endpoint for web UI assets only. It does not update firmware and does not erase NVS configuration. The image must fill the web UI partition exactly; one for a different layout gets 400 before anything is erased. Responses are as for `/api/update`; its restart can't be cancelled and waits only for the roof.

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

Add `event=<event>&level=<1-4>&sound=<pushover sound>` to send a sample of that event (title "Test: ...") at that level and sound instead. Events: `mount_not_parked`, `roof_not_closed` (critical: sent at level 3 or above), `move_fault`, `supply_low`, `unsafe_closing`, `roof_closed`, `daylight_open`, `daylight_closed`, `daylight_unknown`, `safety_lost`, `client_lost`, `client_back`, `client_disconnected`. `title` and `message` (up to 80 / 240 characters) try out custom wording with `{variables}`.

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

RoofRoof is an ASCOM Alpaca server with one device, the roof as a Dome - see [ASCOM Alpaca](../user-guide/alpaca.md) for each member and its refusals. Alpaca has no password (the ASCOM design); its open and close go through the same safety checks as the REST commands. The HTTP surface (all under the same port-80 server as the rest of the API, response envelope per the [ASCOM Alpaca API spec](https://ascom-standards.org/api/)):

| Endpoint | Purpose |
|----------|---------|
| `GET /management/apiversions` | Supported Alpaca API versions |
| `GET /management/v1/description` | Server description: `Manufacturer` `RoofRoof`, `ServerName` the device name |
| `GET /management/v1/configureddevices` | The configured devices: the Dome (`DeviceType` `Dome`, number 0, `UniqueID` `roofroof-<MAC>-dome-0`) while Alpaca is on, else an empty list |
| `GET /setup` | Redirects to **Settings → Network → ASCOM Alpaca** in the web UI |
| `/api/v1/dome/0/...` | The Dome's members ([ASCOM Alpaca](../user-guide/alpaca.md#what-the-dome-does)); any other device: HTTP `400` |

A UDP listener on port `32227` answers Alpaca discovery broadcasts (`alpacadiscovery1` → `{"AlpacaPort":80}`) whenever Alpaca is enabled in settings - this requires a device restart to start/stop.
