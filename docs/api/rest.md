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
- **Busy**: while the device is short of memory (many clients reading at once), it answers `503` with `Retry-After: 2` and `{"error": "The device is busy: try again"}` instead of risking a restart; try again after the given seconds. The ASCOM Alpaca API, the roof commands (`POST /api/roof/...`, Stop among them) and the firmware uploads are never refused this way; they get `503` only if their own reply can't be built. `connections.shortOfMemory` in the status counts these answers.

!!! note "The same in every language"
    The UI language never changes the API. Numbers are JSON numbers with `.` as the decimal point and no thousands separators (`12.1`, never `"12,1"`). Field names and values such as `status`, `event` and `reason` are never translated, and timestamps are Unix seconds or ISO 8601. Text for people to read - alert `title` and `message`, `error` - is English and may be reworded between releases: key on the alert `event` and the other machine fields instead.

---

## Endpoints

### `GET /api/status`

Firmware, memory, flash, partitions, time, WiFi, Ethernet, the pins in effect, MQTT, NTP, alerts, Alpaca, connections and the roof.

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
  "ethernet": { "present": false, "started": false, "phyFound": false, "link": false, "speedMbps": 0, "fullDuplex": false, "ip": "", "mac": "", "ipv6": [] },
  "pins": { "preset": "devkit", "board": "esp32-devkit", "valid": true, "restartNeeded": false },
  "alerts": { "armed": true, "mode": "any", "reason": "none", "since": null, "sinceAgeMs": null, "recentRevision": 7 },
  "alpaca": { "enabled": true, "clients": { "dome": { "connected": true, "watching": true, "silent": false, "lastCheckedAgeMs": 1200, "clientId": 4711 } } },
  "mqtt": { "enabled": true, "connected": true, "broker": "192.168.1.10", "port": 1883, "topic": "roofroof", "availabilityTopic": "roofroof/availability" },
  "connections": { "tcpLimit": 16, "liveUpdates": { "status": 1, "limitPerEndpoint": 3, "replaced": 0, "stalledClosed": 0 }, "alpacaRestoredAfterRestart": false, "shortOfMemory": { "refused": 0, "skippedFrames": 0 } },
  "roof": { "state": "closed", "positionKnown": true, "positionMm": 0, "positionPercent": 0, "target": null, "driving": false, "fromUnknown": false, "moveElapsedMs": 0, "moveTimeoutMs": 0, "hall": { "level": "low", "counts": 712 }, "supplyVolts": 12.46, "supplyLow": false, "minSupplyVolts": 11, "currentSense": false, "motorAmps": null, "maxCurrentAmps": 10, "parkSwitches": { "fitted": 1, "switch1": "closed", "switch2": "notFitted", "switchesOnly": false, "openings": [0, 0], "check": null }, "closePending": false, "calibrating": false, "closePhase": "idle", "closeRearmSeconds": null, "parkWait": null, "parkWaitEnded": null, "override": [], "openOverridable": [], "weather": "safe", "weatherText": "safe", "parkConfirmed": true, "parkText": "parked", "daylight": { "mode": "alert", "state": "night", "sunAltitudeDeg": -14.2, "limitDeg": 0, "text": "Sun -14.2° < 0.0°", "closes": false }, "fault": null, "watchdogTrips": 0, "maxStepGapMs": 21, "maxWatchdogBeatAgeMs": 12, "simulation": "off", "nina": { "set": true, "state": "good", "reachable": true, "connected": true, "atPark": true, "ageSeconds": 2, "text": "" }, "sqmeter": { "on": true, "state": "good", "lastReplySeconds": 3, "text": "Replied 3 s ago: safe" } }
}
```

Shortened above: the document also carries heap and stack figures, `partitions` (running and next slot, web UI partition, NVS usage), `ntp` and the reset reason. The JSON Schema is `status.schema.json` and the [API Reference](reference.md) lists every field.

`firmware.product` is `roofroof` and `firmware.layout` is the partition layout (`l2`); an upload made for another product or layout is refused ([Updates](../user-guide/ota.md)).

`connections` shows who holds the device's TCP connections: the live-update clients on `/ws/status`, the most each endpoint takes, and how many were closed since the restart because a newer one replaced them (`replaced`) or they stopped reading (`stalledClosed`). `shortOfMemory` counts, since the restart, the requests answered "busy" (`refused`) and the live messages skipped (`skippedFrames`) because the device was short of memory.

`alerts` is the [`/api/alerts/armed`](#get-apialertsarmed-post-apialertsarm-post-apialertsdisarm) object, plus `recentRevision`, which goes up whenever [`/api/alerts/recent`](#get-apialertsrecent) would answer differently. `configRevision` goes up whenever the settings are saved, and starts from a random value at each start (so a page loaded before a restart never matches one after it). `recentRevision` restarts from 0 after a restart. Both only say *that* something changed, so a page fetches the details then. `alpaca.clients` has one entry per Alpaca device, keyed by its type: `dome`.

`ethernet` is the wired network on a board that has it (the WT32-ETH01; [Network](../user-guide/network.md#ethernet-in-the-status)): `mode` (the network mode in effect since the start: `auto`, `ethernet` or `wifi`; always `wifi` on a board without Ethernet, and a saved change shows here only after the restart), `present` (the board in effect has Ethernet and the network mode isn't WiFi only), `started`, `phyFound` (the LAN8720 answered), `link`, `speedMbps`, `fullDuplex`, `ip` (`""` without an address), `mac` and `ipv6` (its IPv6 addresses, with IPv6 on). On a DevKit `present` is false and the rest are empty. The `wifi` block is unchanged; on a board running Ethernet with no WiFi saved its `connected` is simply false.

`pins` is the board and pins in effect ([Pins](../user-guide/pins.md)): `preset` (`devkit`, `wt32-carrier-v1`, `custom` or `unset`), `board` (the chosen board, or the one the boot probe found while none is chosen), `valid` (the pins passed the rules at boot; false means nothing is driven) and `restartNeeded` (the saved pins differ from these). The details are in [`GET /api/pins`](#get-apipins).

`time.epoch` is Unix seconds like every other timestamp (`0` until the clock is set); `time.iso` is the device's local time with its UTC offset, for display.

`roof` is the roof as its control task last saw it (the same object is published on MQTT as `<base>/state`, [MQTT](../user-guide/mqtt.md#the-roof-document)) ([Roof motion](../user-guide/roof-motion.md)):

| Field | Meaning |
|---|---|
| `state` | `unknown`, `closed`, `open`, `opening`, `closing`, `stopped` or `error` |
| `positionKnown`, `positionMm`, `positionPercent` | 0 is closed; `null` while the position is unknown |
| `target` | `open` or `closed` while opening or closing, else `null` |
| `driving` | the motor driver is on (false during the 500 ms dead time) |
| `fromUnknown` | the move started from an unknown position (slow, to the first magnet); a person's, or the [automatic close's](../user-guide/close-on-unsafe.md#closing-when-the-position-is-unknown) |
| `blind` | spec 030: the move is a [blind close](../user-guide/roof-motion.md#blind-close): the Hall sensor is dead (a Hall rail fault is latched) and the automatic close drives towards closed on time alone, at the slow duty. `state` reads `closing` while it runs and `error` again afterwards |
| `moveElapsedMs`, `moveTimeoutMs` | time driven in this move and its limit |
| `hall.level`, `hall.counts` | `unsettled`, `low`, `between`, `high` or `rail`, and the raw 12-bit reading ([calibration](../hardware/calibration.md)) |
| `supplyVolts`, `supplyLow`, `minSupplyVolts` | the 12 V supply (`null` when unreadable), whether it is unreadable or below the minimum, and the minimum |
| `currentSense`, `motorAmps`, `maxCurrentAmps` | current sense on, the motor current (`null` while off or unreadable) and its limit |
| `parkSwitches` | `fitted` (0-2) and each switch: `closed`, `open`, `unreadable` or `notFitted`; `switchesOnly`: the park switches are the only park source (`park.source` `switches`); `openings`: for each switch, roof openings since it last read open (kept across restarts); `check`: with the switches only, why Open is refused because a switch hasn't opened in the last 3 roof openings (`parkSwitchCheck`), else `null` ([Safety inputs](../user-guide/safety-inputs.md#park-switches-only)) |
| `closePending` | a close was accepted and waits for an upload or MQTT connect to finish |
| `calibrating` | a [calibration run](../user-guide/calibration-run.md) is under way, pauses at each end included; open and close are refused (`calibrating`) and the Stop bar shows |
| `closePhase` | the [automatic close](../user-guide/close-on-unsafe.md)'s phase (`cannotMove` now means a fault the blind close can't answer, a blind close that has ended, or a roof controller that didn't start; an unknown position closes slowly instead): `idle`, `waitPark`, `closing`, `retryWait`, `heldByPerson`, `cannotMove` or `halted` |
| `weatherHold` | spec 030: the automatic close's [weather hold](../user-guide/close-on-unsafe.md#the-weather-hold): `countedSeconds` (unsafe time added up), `leftSeconds` (until the close is wanted; `-1` while it isn't counting down, or once it has fired) and `fired` (the close is wanted). The verdict, `weather` and the refusal of Open are not delayed by it. Only in the status document, not on MQTT |
| `needsPerson` | spec 030: `active` (the roof needs a person: a restart with the position unknown or a fault latched, a Hall sensor failure, the automatic close unable to move; also the MQTT state's `roof.needsPerson.active`) and `text` (what for and what to do; `""` when not active; only in the status document) |
| `closeRearmSeconds` | spec 026: while the automatic close stands down (`heldByPerson`, `halted`), the seconds until it ends by itself if the weather stays safe and daylight doesn't want the roof closed; `null` while it isn't counting down (idle, or something wants the roof closed now) |
| `parkWait`, `parkWaitEnded` | spec 027: `null`, or while an Open or Close waits for the mount to park `{ "command": "open"\|"close", "origin": "person"\|"alpaca"\|"mqtt"\|"automatic", "leftSeconds", "asked" }` (`leftSeconds` until the wait gives up, `asked`: N.I.N.A. has been asked to park); `parkWaitEnded` is the text of the last wait that ended without running (it gave up, or another check refused the command), or `null`. An Alpaca client reads that as an error until its next command |
| `override`, `openOverridable` | spec 027: the names (`weatherNotSafe`, `daylight`, `mountNotParked`) of the person's confirmed overrides in effect now, and of the refusals a person may override for an Open now (`weatherNotSafe` only while SQMeter alone says unsafe: never for rain or a source in doubt) |
| `nina` | spec 027: the park source's health: `set` (an address is saved), `state` (`notConfigured`, `waiting`, `good`, `unreachable`, `unreadable` or `apiError`), `reachable`, `connected` (the mount is connected in N.I.N.A.), `atPark`, `ageSeconds` since the last reading and `text` (why, when it isn't good) |
| `sqmeter` | spec 027: the weather source's health: `on` (a source is switched on), `state` (`off`, `waiting`, `good`, `stale`, `unreachable`, `unreadable` or `offline`: as in [`GET /api/roof/inputs`](#get-apiroofinputs)), `lastReplySeconds` (`null` without a good reply) and `text` |
| `weather`, `weatherText` | the weather verdict the roof's command checks use now (`safe`, `unsafe` or `unknown`; unknown and unsafe when the main loop hasn't handed it over for 5 s) and its reason, at most 200 bytes |
| `parkConfirmed`, `parkText` | whether a close would pass the park check now (N.I.N.A. and every fitted switch), and why, at most 200 bytes |
| `daylight` | the [daylight failsafe](../user-guide/close-on-unsafe.md#daylight-failsafe): `mode` (`off`, `alert` or `close`), `state` (`off`, `unknown`, `night` or `day`), `sunAltitudeDeg` (`null` while off or unknown), `limitDeg` (the threshold), `text` ("Sun 3.2° ≥ 0.0°", "Sun -4.1° < 0.0°", "Location not set", "Clock not set" or "Off") and `closes` (daylight wants the roof closed now: mode `close` and state `day`) |
| `testBox` | the [test box](../user-guide/test-box.md) bypasses, always present (see below) |
| `fault` | `null`, or the latched fault: `kind`, `cause` (`logic`, `watchdog`, `watchdogStopped`, `parkInterlock`, `storedUnreadable`, `benchSupply`, `pinSetup`), `measured`, `limit` and `text`. `pinSetup` means nothing is driven (no board chosen, or an invalid or unreadable pin set), with the text "Pin setup invalid: " and the reason, or just "No board chosen yet: choose the board and its pins"; it isn't stored and can't be cleared, only fixed with a valid pin set and a restart ([Pins](../user-guide/pins.md#when-nothing-is-driven)) |
| `simulation` | `off` on a real roof; `bench` for the bench-mode firmware (a simulated roof, the motor never driven; see [Bench mode](../user-guide/bench-mode.md)); `demo` in the online demo |
| `watchdogTrips`, `maxStepGapMs`, `maxWatchdogBeatAgeMs` | since the restart: how often the watchdog switched the driver off, the longest gap between control steps, and the oldest watchdog heartbeat a step saw (the driver needs it at most 50 ms old) |

`stackFree.roof` is the roof control task's unused stack in bytes.

`roof.testBox` says which [test box](../user-guide/test-box.md) bypasses are set and which are in effect now:

```json
"testBox": {
  "set": true, "active": true, "park": true, "weather": true, "daylight": false,
  "until": 1764160000, "expired": false, "waitingForClock": false, "parkRequests": false,
  "text": "Test box: park check (no telescope), weather check (no weather source) bypassed until 2026-11-26 12:26 UTC"
}
```

`park`, `weather` and `daylight` are the bypasses in effect now; `set` is true when any is set in the settings; `until` is the deadline in Unix seconds (`0` with nothing set). When the deadline has passed, `expired` is true and the checks are back; while the clock isn't set, `waitingForClock` is true and the checks apply. `text` is empty with nothing set; otherwise it names the bypasses and the deadline, or says they expired or wait for the clock. With nothing set every bypass boolean is false. `parkRequests` is true when unsafe weather would ask a configured N.I.N.A. to park its mount (`park.requestPark` on and an address set), whatever the bypasses. While a bypass is in effect, `weatherText` and `parkText` end in "bypassed (test box): no weather source" and "bypassed (test box): no telescope under this roof", `weather` is `safe` and `parkConfirmed` is true.

`location` says where the daylight failsafe's site comes from ([Site from SQMeter](../user-guide/close-on-unsafe.md#site-from-sqmeter)):

```json
"location": { "source": "sqmeter", "set": true, "gps": true, "savedAt": 1763540000, "readAgeSeconds": 180, "problem": null }
```

| Field | Meaning |
|---|---|
| `source` | `sqmeter` (read from the SQMeter at the weather source's address) or `manual` (typed in) |
| `set` | a location is known; without one daylight's state is unknown ("Location not set") |
| `gps` | the kept location was SQMeter's GPS fix |
| `savedAt` | Unix seconds when the location read from SQMeter was last saved; `null` when never, when the clock wasn't set, or with `manual` |
| `readAgeSeconds` | seconds since SQMeter's location was last read in this run; `null` before the first good read, or with `manual` |
| `problem` | why SQMeter's location isn't being read now: `noAddress` (the weather source address isn't set), `waiting` (not read yet), `unreachable`, `unreadable`, `noLocation` (SQMeter has none), `passwordNeeded` (SQMeter wants a password) or `passwordRefused` (SQMeter refused the password); `null` when the last read was good, or with `manual` |

SQMeter's location is never a safety input: it only sets where the sun's altitude is worked out for.

`sync` says how the settings groups that follow SQMeter stand ([Keeping in step](../user-guide/close-on-unsafe.md#keeping-in-step)):

```json
"sync": { "problem": null, "readAgeSeconds": 180, "secrets": "received",
  "time": { "follow": true, "syncedAt": 1763540000, "refused": null, "needs": [] },
  "mqtt": { "follow": true, "syncedAt": 1763540000, "refused": null, "needs": ["mqttPassword"] },
  "alerts": { "follow": false, "syncedAt": null, "refused": null, "needs": [] } }
```

| Field | Meaning |
|---|---|
| `problem` | why SQMeter's settings aren't being read now (`noAddress`, `waiting`, `unreachable`, `unreadable`, `passwordNeeded`, `passwordRefused`), `null` when the last read was good or no group follows |
| `readAgeSeconds` | seconds since SQMeter's settings were last read in this run, `null` before the first |
| `secrets` | the last read of SQMeter's passwords (`GET /api/secrets`) while the MQTT or alerts group follows: `received`, or why not (`waiting`, `notSupported` (an older SQMeter), `sharingOff`, `noWebPassword`, `loginNeeded`, `loginRefused` (not asked again until SQMeter's saved login changes), `tooManyLogins` (SQMeter's 429), `unreachable`, `unreadable`); `null` while neither group follows or SQMeter's address isn't set |
| `<group>.follow` | the group follows SQMeter (`sqmeterSync.<group>.follow`) |
| `<group>.syncedAt` | Unix seconds of the group's last change from SQMeter, `null` when never |
| `<group>.refused` | why the last sync didn't take SQMeter's values (the old ones are kept), e.g. a value the settings' validation refuses or "MQTT roof commands are on: set the broker here, not from SQMeter"; `null` otherwise |
| `<group>.needs` | secrets to enter because SQMeter has one set and RoofRoof's is empty: `mqttPassword`, `ntfyToken`, `pushoverKeys` (Pushover is never synced, so its keys are asked for while either is missing) |

---

### `GET /api/config`

Read the full device configuration. Password fields are returned as `********` when a stored password exists.

While the device is short of memory (several pages reading at once) it answers `503` instead of risking a restart; try again. A save (`POST /api/config`) is refused the same way, and nothing is saved.

Every field, with its default, is in the [configuration reference](../user-guide/configuration.md#full-configuration-reference), including the `roof` section that sets how the roof moves ([Roof motion](../user-guide/roof-motion.md)).

```bash
curl http://roofroof.local/api/config
```

The response header `X-Config-Revision` is the settings revision the document was read at (the same counter as `configRevision` in the status). Send it back as `baseRevision` to have a save checked against changes made since ([below](#post-apiconfig)).

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

`mqtt.password`, `alerts.ntfy.token`, `alerts.pushover.userKey` and `alerts.pushover.appToken` may be sent as `"(from SQMeter)"`: the save then takes the secret the device received in the last [copy from SQMeter](#post-apisqmetersite), held for 30 minutes, but only while the field's server is the one SQMeter uses it for (the MQTT broker, port and user name; the ntfy server). Otherwise the save is refused with `400`, e.g. `{"error": "The MQTT password from SQMeter is for SQMeter's own broker, not this one: copy again, or type it"}` or `{"error": "A password from SQMeter is no longer held: copy again, or type it"}`. The placeholder is never stored.

`park.source` and `park.switches` must agree: `nina` with `none`, `switches` or `ninaAndSwitches` with `one` or `two` (`{"error": "Park: park switches need one or two fitted; choose N.I.N.A. only for none"}`). A body with `park.switches` but no `park.source` sets the source the switches meant before it existed (`none`: `nina`, otherwise `ninaAndSwitches`).

Every value must have the setting's type: text for names and addresses, `true`/`false` for switches, numbers for numbers. A value of the wrong type, `null` included (other than `null` to clear a password), is rejected with `400` and an error naming the setting, e.g. `{"error": "Settings: wifi.ssid must be a string"}`; it is never replaced by a default. A setting you leave out keeps its current value.

While the device has a WiFi network saved, `wifi.ssid` can't be set to `""`: the save is rejected with `"WiFi network can't be blank; use WiFi setup to change networks"`, so a settings save never takes the device off its network. Join a different network with [`POST /api/wifi/connect`](#post-apiwificonnect) (or pick it in Settings → Network); there is no endpoint that forgets the network.

Successful saves return the new settings revision:

```json
{ "success": true, "revision": 1834201978 }
```

#### Test box settings

The `testBox` group (`park`, `weather`, `daylight`, `hours`, `until`; see [Configuration](../user-guide/configuration.md#test-box)) follows these rules:

- `until` is read-only: a value in a save body is ignored. The device stamps it.
- A bypass turning on, or `testBox.hours` sent while one is on, stamps a new deadline. To **renew**, send `{"testBox": {"hours": 72}}`.
- A save that turns `testBox.park` on clears `park.nina.url` in the same save (even an address sent in that save); turning it off later doesn't restore it.
- A save is refused with `400` and an `error`:
    - `Test box: "No telescope under this roof" is on; turn it off before setting a N.I.N.A. address` when `testBox.park` stays on and the save sets `park.nina.url`;
    - `Test box: the weather bypass is for a box with no weather source; turn SQMeter HTTP and MQTT off first (or turn the bypass off)` when `testBox.weather` would be on with `safety.http.enabled` or `safety.mqtt.enabled`;
    - an error naming the range when `testBox.hours` isn't 1 to 720;
    - `Test box: the clock isn't set yet (Settings → Time); a bypass needs a deadline` when a bypass turns on while the clock isn't set.
- A backup restore never brings a deadline.

#### Saves from a page loaded earlier

The web UI sends only the settings the person changed, plus two keys the device checks and never stores:

```json
{
  "wifi": { "ipv6": false },
  "baseRevision": 1834201977,
  "baseValues": { "wifi": { "ipv6": true } }
}
```

- `baseRevision`: the `X-Config-Revision` its copy was read at. When it is current, the save goes through.
- `baseValues`: the values it read for each field it changes, shaped like the settings (`null` for one it didn't have). For a credential, its partners come too: `wifi.ssid` with `wifi.password`; the MQTT broker, port, user name and password; the web user name and password; the ntfy server, topic and token; the Pushover keys; the webhook address and header; SQMeter's address, user name and password.

When the revision isn't current, the device compares each base value with its own settings as `GET /api/config` shows them (passwords masked). If all match, the save goes through: a change made elsewhere to other settings, by another page or by the device itself ([following SQMeter](../user-guide/close-on-unsafe.md#site-from-sqmeter)), is kept and doesn't block it. If any differ, nothing is saved and the answer is `409` naming them (at most 16):

```json
{
  "error": "Settings changed on the device since this page loaded them: park.nina.url",
  "conflicts": ["park.nina.url"],
  "revision": 1834201980
}
```

A save without `baseRevision` (a script, `curl`, an older page) isn't checked, as before. With `baseRevision` but no `baseValues`, any other revision is refused with an empty `conflicts`. The check and the save run under the same lock as every other settings save, so nothing can land in between.

!!! note
    `POST` is the primary method. The firmware also accepts `PUT` for compatibility with full-resource settings clients.

While the roof is opening or closing, every settings save is refused with `409` and `{"error": "The roof is moving: stop it first, then try again"}`: writing the flash pauses the processor's code cache, and with it the roof's control task and watchdog. A save is also refused while a close is waiting to start (`"A close is waiting to start: try again once the roof has closed"`), and while it is being written an open is refused (`busy`) and a close waits for it. Restores, uploads, WiFi changes and restarts are refused the same way; restarts, uploads, WiFi changes and scans also while the automatic close is waiting for the park or closing (`"The automatic close is under way: try again once the roof has closed or the weather is safe"`). The full table is in [Roof motion](../user-guide/roof-motion.md#while-the-roof-moves). Settings saved while the roof is stopped part-way make its position unknown (the estimate used the old ones).

A save that changes `pins` or `network.mode` is also refused with `409` while the roof moves, a close is waiting, the automatic close is under way, or a calibration run or a jog is running: `{"error": "Pins and the network mode can't change while the roof is busy"}`. Both apply at the next restart. A pin set that breaks the [pin rules](../user-guide/pins.md#the-pin-rules), checked against this chip, is refused with `400` and the first error, e.g. `{"error": "RPWM on GPIO35: GPIO35 is input only and can't drive an output"}`. On a device that has seen its LAN8720 answer (Ethernet started, or the boot probe found it, also in WiFi only), a pin change to any board but the WT32-ETH01 is refused with `400` `{"error": "This board is a WT32-ETH01 (its Ethernet answered): choose a WT32-ETH01 board"}`; a named `pins.preset` sent alone fills in its pins, and with different pins it is refused. `safety.rainContact` no longer exists: a body that still has it is accepted and the key ignored.

A setting whose dependency is off is saved as sent and reported inactive by `GET /api/settings/effective`; it isn't rejected. Only combinations that can never work are refused, with the reason in `error` (see [Settings dependencies](../reference/settings-dependencies.md)).

While `location.source` is and stays `sqmeter` and the device has a location set, a save keeps the device's own `location.set`, `latitude` and `longitude` (the values are still checked); `location.sqmeter` is the device's and is never read from a save. A `location` without `source` is `manual` when `set` is true, else `sqmeter`.

---

### `POST /api/sqmeter/site`

Reads the SQMeter's settings for the copy in Settings and the setup wizard ([Site from SQMeter](../user-guide/close-on-unsafe.md#copy-from-sqmeter)). The device fetches the SQMeter's `GET /api/config` itself (SQMeter sends no CORS headers, so a browser can't) and answers with only what can be copied. Needs the password when protection is on. Nothing is saved.

```bash
curl -u admin:password -X POST http://roofroof.local/api/sqmeter/site \
  -H "Content-Type: application/json" -d '{"url": "192.168.1.50"}'
```

`url` is optional: without it, the saved `safety.http.url`. It takes the address setting's forms (`192.168.1.50`, `sqmeter.local`, `https://sqmeter.lan`, or a URL ending in one of SQMeter's API paths); only its scheme, host and port are used, with SQMeter's own path. `https` is encrypted, without a certificate check. `username` and `password` are optional too: SQMeter's login for this request (typed in the wizard before saving). Without them the saved login is used, and an empty or masked (`********`) password means the saved one, but only when `url` has the saved address's host and port: a saved password is never sent to another device. The login goes as HTTP Basic, as on every request RoofRoof makes to SQMeter.

```json
{
  "address": "http://192.168.1.50",
  "device": { "name": "SQMeter" },
  "location": { "set": true, "latitude": 51.5074, "longitude": -0.1278 },
  "time": { "timezone": "GMT0BST,M3.5.0/1,M10.5.0", "server1": "pool.ntp.org", "server2": "time.nist.gov" },
  "mqtt": { "broker": "192.168.1.10", "port": 1883, "username": "observatory", "topic": "sqmeter", "passwordSet": true, "enabled": true },
  "ntfy": { "server": "https://ntfy.sh", "topic": "my-observatory", "tokenSet": false, "enabled": true },
  "pushover": { "userKeySet": true, "appTokenSet": true, "enabled": false },
  "secrets": { "problem": null, "http": true, "mqttPassword": "received", "ntfyToken": "none", "pushover": "same" }
}
```

A secret is only ever reported as set or not (`passwordSet`, `tokenSet`, `userKeySet`, `appTokenSet`): its value never appears, even from an SQMeter old enough not to mask it. `enabled` is SQMeter's own on/off switch for MQTT and each channel.

`secrets`: when `url` is the saved SQMeter address (its scheme, host and port), the device also reads SQMeter's `GET /api/secrets` with the same login and holds what it gets, in memory, for 30 minutes. Each of `mqttPassword`, `ntfyToken` and `pushover` (both keys) is `received` (held, and different from RoofRoof's saved one), `same` (equal to it) or `none` (not received, or not set on SQMeter). `problem` is `null` when they were received, else why not: `notSaved` (the address isn't the saved one), `notSupported` (SQMeter answered 404: it is older), `sharingOff` or `noWebPassword` (SQMeter's two 403s), `loginNeeded` or `loginRefused` (401), `tooManyLogins` (429: SQMeter locked it for a minute after failed logins), `unreachable`, `unreadable`; with SQMeter's `sharing.secrets` off nothing is asked and it is `sharingOff`. `http` is true when they crossed the network unencrypted. To use a held secret, save its field as `"(from SQMeter)"` ([`POST /api/config`](#post-apiconfig)). A part an older SQMeter doesn't have is `null`; SQMeter v0.1's top-level `timezone` is read as the time zone.

| Status | When |
|---|---|
| 400 | no address set, or not a valid address |
| 409 | the roof moving, a close waiting, the automatic close working, or another read of SQMeter running |
| 502 | SQMeter not answering (`"SQMeter not answering: connection refused"`), turning the request away (`"SQMeter wants a password"` with no login sent, `"SQMeter refused the password"` with one), another HTTP status, or not SQMeter's settings |
| 503 | not enough memory to start the fetch |

The fetch runs on its own short-lived task for at most about 5 s; the web server answers when it is done.

---

### `POST /api/sqmeter/test`

The **Test** next to SQMeter's address in Settings: the device reads SQMeter's `GET /api/safety` once, as the weather poll does, at the body's address and login (the same body as `POST /api/sqmeter/site`; only the address's scheme, host and port are used, whatever path it has), and answers with SQMeter's verdict. Nothing is saved, and the weather source is unchanged. Needs the password when protection is on.

```bash
curl -u admin:password -X POST http://roofroof.local/api/sqmeter/test \
  -H "Content-Type: application/json" -d '{"url": "192.168.1.50"}'
```

```json
{ "safe": false, "reasonFlags": 512, "reasons": "Rain 4.2 mm/h", "text": "SQMeter says unsafe: Rain 4.2 mm/h" }
```

| Status | When |
|---|---|
| 200 | SQMeter answered: `safe`, `reasonFlags`, `reasons` (SQMeter's own reasons text, joined with "; ", at most 160 bytes; `""` when it sent none) and `text` (SQMeter's reasons, or without them the flags as words: "rain") |
| 400 | no address set, or not a valid address |
| 409 | the roof moving, a close waiting, the automatic close working, or another read of SQMeter running |
| 502 | `error` says why: `"SQMeter not answering: connection refused"`, `"SQMeter wants a password"`, `"SQMeter refused the password"`, `"That address answered HTTP 404, not like SQMeter's /api/safety"`, or `"That address answered, but not like SQMeter's /api/safety (missing safe)"` (another of SQMeter's pages, say) |
| 503 | not enough memory to start the read |

### `POST /api/nina/test`

Spec 027: the **N.I.N.A. Test** next to N.I.N.A.'s address in Settings. The device reads N.I.N.A.'s mount information once, as the park check does, at the body's `url` or, without a body, the saved address, and says what it found. The address starts with `http://` or `https://`. Nothing is saved. Needs the password when protection is on.

```bash
curl -u admin:password -X POST http://roofroof.local/api/nina/test \
  -H "Content-Type: application/json" -d '{"url": "https://nina.local:1888"}'
```

```json
{ "reachable": true, "connected": true, "atPark": false, "slewing": false }
```

| Status | When |
|---|---|
| 200 | N.I.N.A. answered: `reachable`, `connected` (the mount is connected in N.I.N.A.), `atPark` and `slewing` |
| 400 | a body that isn't `{"url": "..."}` (a string, at most 512 bytes), no address, or an address that doesn't start with `http://` or `https://` |
| 409 | the roof is busy, or another read runs |
| 502 | `error` says why, with `reachable` false when nothing answered: `"N.I.N.A. not responding: connection refused"`, or `"N.I.N.A. answered, but: ..."` |
| 503 | not enough memory to start the read |

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

`settings` is what applying saves, masked like `GET /api/config` (shortened above). `secrets.state` is `restored` (passphrase right), `kept` (the file has secrets, no passphrase given: this device keeps its own) or `none`. `notApplicable` lists settings that aren't applied, each with its `path`, the backup's `value` and a `reason`; they keep this device's values. Reasons: `pushover-keys` (Pushover on without its keys), `board-pins` (`pins`: a restore never changes the board or the pins, which belong to this hardware, so a backup whose pins differ from this device's lists them here) and `board-analog` (`analog`: nor the dividers and trims, which describe this board's resistors; listed when they differ). `restart` says why applying restarts the device (empty: it doesn't): `wifi` (the WiFi settings or `network.mode`), `ota`, `ipv6` or `alpaca`. A restore never gives `pins` (a settings save can). The web UI works out the per-setting differences from `settings`.

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

A board running Ethernet with the network mode **Ethernet only** keeps its WiFi radio off: the scan is refused with `409` `{"error": "WiFi is off: the network mode is Ethernet only"}` ([Network](../user-guide/network.md#the-network-mode)).

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

`400 {"error": "Missing SSID or password"}` if either field is absent (send `""` for an open network). `409` with the reason while the roof is opening or closing, a close is waiting to start or the automatic close is waiting for the park or closing: trying a network drops the current one, and with it N.I.N.A. A scan (`GET /api/wifi/scan`) is refused the same way. While either runs (at most 15 s), an open is refused (`busy`) and a close waits for it. In **Ethernet only** both are refused with `409` `{"error": "WiFi is off: the network mode is Ethernet only"}`.

---

### `POST /api/restart`

Restart the device. Refused with `409` and the reason while the roof is opening or closing, a close is waiting to start, or the automatic close is waiting for the park or closing.

```bash
curl -X POST http://roofroof.local/api/restart
```

---

## Pins

The board, the roof's pins and the wiring check ([Pins](../user-guide/pins.md)). The pins themselves are saved as `pins` with the other settings ([`POST /api/config`](#post-apiconfig)) and apply at the next restart; the analog front end is saved as `analog` and applies without a restart, once the roof is still.

### `GET /api/pins`

The pins in effect and saved, the rules' report, the presets and the boards. Open (no password), like `/api/status`: settings and rules only, no secret.

```bash
curl http://roofroof.local/api/pins
```

```json
{
  "running": { "preset": "devkit", "board": "esp32-devkit", "rpwm": 25, "lpwm": 26, "enable": 27, "currentR": 34, "currentL": 35, "supply": 36, "hall": 39, "parkA": 32, "parkB": 33 },
  "saved": { "preset": "devkit", "board": "esp32-devkit", "rpwm": 25, "lpwm": 26, "enable": 27, "currentR": 34, "currentL": 35, "supply": 36, "hall": 39, "parkA": 32, "parkB": 33 },
  "restartNeeded": false,
  "valid": true,
  "reason": "",
  "issues": [],
  "psram": false,
  "board": "esp32-devkit",
  "detected": null,
  "presets": [
    { "preset": "devkit", "board": "esp32-devkit", "rpwm": 25, "lpwm": 26, "enable": 27, "currentR": 34, "currentL": 35, "supply": 36, "hall": 39, "parkA": 32, "parkB": 33 },
    { "preset": "wt32-carrier-v1", "board": "wt32-eth01", "rpwm": 14, "lpwm": 15, "enable": 4, "currentR": 35, "currentL": 32, "supply": 36, "hall": 39, "parkA": 33, "parkB": 5 }
  ],
  "boards": [
    { "board": "esp32-devkit", "ethernet": [], "header": [] },
    { "board": "wt32-eth01", "ethernet": [0, 16, 18, 19, 21, 22, 23, 25, 26, 27], "header": [2, 4, 5, 12, 14, 15, 17, 32, 33, 35, 36, 39] }
  ],
  "analog": {
    "preset": "devkit-bench",
    "supply": { "top": 100000, "bottom": 22000, "gain": 1, "offsetVolts": 0 },
    "hall": { "top": 10000, "bottom": 20000 },
    "currentR": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 },
    "currentL": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 }
  },
  "analogIssues": [
    { "input": "currentR", "error": false, "text": "R_IS divider: at its highest (5.0 V) the input would put 3.33 V on the pin, over 3.3 V" },
    { "input": "currentL", "error": false, "text": "L_IS divider: at its highest (5.0 V) the input would put 3.33 V on the pin, over 3.3 V" }
  ],
  "analogPresets": [
    { "preset": "devkit-bench", "supply": { "top": 100000, "bottom": 22000, "gain": 1, "offsetVolts": 0 }, "hall": { "top": 10000, "bottom": 20000 }, "currentR": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 }, "currentL": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 } },
    { "preset": "wt32-carrier-v1.1", "supply": { "top": 100000, "bottom": 22000, "gain": 1, "offsetVolts": 0.3 }, "hall": { "top": 10000, "bottom": 20000 }, "currentR": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 }, "currentL": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 } }
  ]
}
```

The pin maps (`running`, `saved`, `presets`) also carry `parkAPullup` and `parkBPullup` (`internal` or `external`), left out above.

| Field | Meaning |
|---|---|
| `running` | The pins read at the last start: what the device uses until the next restart |
| `saved` | The pins as saved |
| `restartNeeded` | `saved` differs from `running` |
| `valid`, `reason` | Whether `running` passed the rules at the start. When not, nothing is driven and `reason` says why: the first broken rule, "No board chosen yet: choose the board and its pins" or "The saved pin setup couldn't be read" |
| `issues` | The errors and warnings of `running`: `function` (`rpwm`, `lpwm`, `enable`, `currentR`, `currentL`, `supply`, `hall`, `parkA`, `parkB`, or `null` for one about the whole map), `pin`, `code`, `error` (false for a warning) and `text` |
| `psram` | The chip reports PSRAM. GPIO16 and 17 are reserved on every board either way: this build can't detect a WROVER module's PSRAM, which uses them |
| `board` | The board in effect: the chosen one, or the one the boot probe found while none is chosen |
| `detected` | With no board chosen (or unreadable settings), what the boot probe found: `wt32-eth01` when a LAN8720 answered, else `esp32-devkit`; `null` when the probe didn't run (a board is chosen, or the chip has PSRAM) |
| `presets` | Each named preset's board and pins |
| `boards` | Each board's Ethernet pins and the pins its header brings out (`[]`: every existing GPIO) |
| `analog` | The saved [analog front end](../user-guide/pins.md#advanced-the-analog-front-end) (the settings' `analog` group). It applies without a restart, so there is no running copy |
| `analogIssues` | The device's checks on `analog`: `input` (`supply`, `hall`, `currentR`, `currentL`, or `""` for one about the whole setup), `error` (false for a warning: a divider that would put more than 3.3 V on its pin) and `text`. The default carries two warnings, on R_IS and L_IS |
| `analogPresets` | The named analog presets' values: `devkit-bench` and `wt32-carrier-v1.1` |

Issue codes, errors: `missing`, `flash`, `psram`, `probe` (GPIO0, 16, 18 or 23, the boot probe's pins, on every board), `uart`, `ethernet`, `header`, `inputOnly`, `notAdc1`, `noPullup`, `shared`, `enStrap`, `presetMismatch`, `boardDetected` (`function` `null`: the device has seen its LAN8720 answer and the map's board isn't `wt32-eth01`, "This board is a WT32-ETH01 (its Ethernet answered): choose a WT32-ETH01 board"); warnings: `strap0`, `strap2`, `strap5`, `strap12`, `strap15`. The texts are on [Pins](../user-guide/pins.md#the-pin-rules).

### `POST /api/pins/validate`

Checks a pin map against the rules for this chip and saves nothing. Open (no password). The body has the `pins` shape; a named preset alone fills in its pins, and fields left out keep the saved values.

```bash
curl -X POST http://roofroof.local/api/pins/validate -H "Content-Type: application/json" \
  -d '{"preset": "custom", "board": "esp32-devkit", "rpwm": 35}'
```

```json
{
  "valid": false,
  "pins": { "preset": "custom", "board": "esp32-devkit", "rpwm": 35, "lpwm": 26, "enable": 27, "currentR": 34, "currentL": 35, "supply": 36, "hall": 39, "parkA": 32, "parkB": 33 },
  "issues": [
    { "function": "rpwm", "pin": 35, "code": "inputOnly", "error": true, "text": "RPWM on GPIO35: GPIO35 is input only and can't drive an output" },
    { "function": "currentL", "pin": 35, "code": "shared", "error": true, "text": "L_IS on GPIO35: GPIO35 is already RPWM" }
  ]
}
```

On a device that has seen its LAN8720 answer (Ethernet started, or the boot probe found it), a map for any board but `wt32-eth01` also gets the `boardDetected` error, as a save that changes the pins would.

The body may also carry `analog`, an analog setup in the settings' shape (read over the saved one; a named preset fills in its values first). The answer then adds `analog` (as read) and `analogIssues`, and `valid` is false when either the pins or the analog setup has an error:

```bash
curl -X POST http://roofroof.local/api/pins/validate -H "Content-Type: application/json" \
  -d '{"preset": "devkit", "analog": {"preset": "custom", "currentR": {"bottom": 22000}}}'
```

```json
{
  "valid": true,
  "pins": { "preset": "devkit", "board": "esp32-devkit", "rpwm": 25, "lpwm": 26, "enable": 27, "currentR": 34, "currentL": 35, "supply": 36, "hall": 39, "parkA": 32, "parkB": 33, "parkAPullup": "internal", "parkBPullup": "internal" },
  "issues": [],
  "analog": { "preset": "custom", "supply": { "top": 100000, "bottom": 22000, "gain": 1, "offsetVolts": 0 }, "hall": { "top": 10000, "bottom": 20000 }, "currentR": { "top": 10000, "bottom": 22000, "gain": 1, "offsetAmps": 0 }, "currentL": { "top": 10000, "bottom": 20000, "gain": 1, "offsetAmps": 0 } },
  "analogIssues": [
    { "input": "currentR", "error": false, "text": "R_IS divider: at its highest (5.0 V) the input would put 3.44 V on the pin, over 3.3 V" },
    { "input": "currentL", "error": false, "text": "L_IS divider: at its highest (5.0 V) the input would put 3.33 V on the pin, over 3.3 V" }
  ]
}
```

An `analog` value that isn't a number where one is needed, an input that isn't an object, or an unknown analog preset, is a `400`. The checks include the roof's saved settings: over-current reach, the broken-wire floor and the supply trim's raise at the minimum supply ([A trim can't defeat a safety check](../user-guide/pins.md#a-trim-cant-defeat-a-safety-check)).

`400` with `{"error": "..."}` for a body that isn't a pin map: not an object, an unknown preset or board, or a pin that isn't a number.

### `POST /api/pins/check`

Starts the [wiring check](../user-guide/pins.md#the-wiring-check). Needs the password when protection is on. It runs on the device's main loop, takes under a second and never moves the roof; read the result with `GET /api/pins/check`.

```bash
curl -u admin:password -X POST http://roofroof.local/api/pins/check
```

`202` `{"started": true}`. `409` with the reason while the roof moves, a close is waiting, the automatic close is under way or a calibration run or a jog is running; while the pins are invalid or no board is chosen; while a saved pin change waits for a restart ("Restart first: the saved pins aren't in effect yet"); or while a check is already running.

### `GET /api/pins/check`

The last wiring check since the start. Open (no password): readings and verdicts only.

```json
{
  "state": "done",
  "checks": [
    { "id": "enPulldown", "level": "pass", "value": null, "unit": "", "text": "EN reads low with the internal pull-up: its pull-down holds the driver off" },
    { "id": "hall", "level": "pass", "value": 2010, "unit": "counts", "text": "2010 counts: a plausible reading" },
    { "id": "supply", "level": "pass", "value": 12.4, "unit": "V", "text": "12.4 V: a plausible supply" },
    { "id": "parkA", "level": "pass", "value": null, "unit": "", "text": "Closed: parked (the switch pulls the input to ground)" },
    { "id": "parkB", "level": "skipped", "value": null, "unit": "", "text": "Not fitted (Settings, Roof, park switches)" },
    { "id": "currentR", "level": "pass", "value": 12, "unit": "mV", "text": "12 mV while stopped: near zero (current sense is off)" },
    { "id": "currentL", "level": "pass", "value": 9, "unit": "mV", "text": "9 mV while stopped: near zero (current sense is off)" },
    { "id": "ethernet", "level": "pass", "value": null, "unit": "", "text": "No Ethernet PHY found, as expected on an ESP32 DevKit" },
    { "id": "motion", "level": "skipped", "value": null, "unit": "", "text": "Direction and travel need the roof to move: use Jog and teach the ends on the calibration page" }
  ],
  "finishedAt": 123456
}
```

`state` is `idle` (not run since the start), `running` or `done`; `checks` is empty until it is `done`. Each check has an `id` (`enPulldown`, `hall`, `supply`, `parkA`, `parkB`, `currentR`, `currentL`, `ethernet`, `motion`), a `level` (`pass`, `warn`, `fail`, `skipped`), the `value` it read with its `unit` (`null` when there is none) and a `text`. On a WT32-ETH01 in WiFi only, `ethernet` is `skipped` ("Not checked: the network mode is WiFi only"); it is skipped too if the roof became busy during the check ("Not checked: the roof became busy, so run the check again once it is still"). `suggestPreset` (`wt32-carrier-v1`) is added when the Ethernet probe found a WT32-ETH01's LAN8720 on a board set up without Ethernet. `finishedAt` is the device's uptime in milliseconds when it finished.

---

## Roof

Each of these needs the password when protection is on and the header `X-RoofRoof: 1` (without it: `403`), and counts as a person's command (a person may move a roof whose position is unknown, slowly, to the first magnet). The header stops another web page open in your browser from moving the roof with a cross-site request: a plain form or simple cross-origin request can't send it. The answer comes after the command has reached the motor driver.

```bash
curl -X POST -H "X-RoofRoof: 1" http://roofroof.local/api/roof/open
```

```json
{ "success": true, "state": "opening" }
```

An open or close may carry a body `{"override": [...]}` (see below) and, when the mount isn't parked but N.I.N.A. can be asked, is accepted with `202` and waits for the park: `{ "success": true, "state": "closed", "pending": true, "message": "Waiting for the mount to park: N.I.N.A. was asked; the open runs once the park holds" }`.

A refusal is `409` with the reason and a text that states the measured value and the limit, or the input that failed:

```json
{ "success": false, "state": "open", "reason": "mountNotParked", "error": "Mount not parked: park switch 1 not closed" }
```

### `POST /api/roof/open`

Open. Refused (`mountNotParked`, spec 027: the telescope passes under the roof either way, so an open needs the park as a close does) unless the mount is confirmed parked, (`weatherNotSafe`) unless the weather verdict is safe, (`daylight`, "Daylight: Sun 3.2° ≥ 0.0°") while `roof.daylight.closes` is true (the weather reason comes first when both apply), (`parkSwitchCheck`) with the park switches only while `roof.parkSwitches.check` is set (a switch hasn't opened in the last 3 roof openings), (`busy`) while an upload, command-line OTA or a settings save is writing the flash, an MQTT connect is under way or a restart is scheduled, (`hotspot`) while the setup hotspot (no password) is on, while nothing is driven (`pinSetup`: "No board chosen yet: choose the board and its pins", or "Pin setup invalid: " and the first broken pin rule or "The saved pin setup couldn't be read"; [Pins](../user-guide/pins.md#when-nothing-is-driven)), and by the motion logic: `faultLatched`, `hallNotSettled`, `supplyLow`, `supplyUnreadable`, `currentUnreadable`, `settingsInvalid`. The motor starts after the 500 ms dead time and stops on the open magnet. An open in progress stops (no fault) as soon as the weather is no longer safe, or `roof.daylight.closes` becomes true, or the park is lost.

**Waiting for the park.** When the park is the only thing refusing the open and N.I.N.A. can be asked (an address is set and **Ask N.I.N.A. to park** is on), the open is accepted with `202` and `pending`: N.I.N.A. is asked to park (again every park request gap), and once the park has held for the park hold time the open runs through every check again. `roof.parkWait` shows the wait. It ends at the park wait limit (`park.timeoutSeconds`) with the critical "mount not parked" alert, on any stop, on a new command, or when another check refuses the command (`roof.parkWaitEnded` says why). When N.I.N.A. can't be asked, the refusal says so: "Mount not parked: ...: no N.I.N.A. address to ask for the park: park the mount, or set N.I.N.A.'s address" or "...: asking N.I.N.A. to park is off (Settings, Roof, N.I.N.A.): park the mount, or turn it on".

**Overrides.** An optional body names the refusals a person confirms for this one command: `{"override": ["weatherNotSafe", "daylight"]}`. `weatherNotSafe` only counts while SQMeter alone says unsafe (never for rain or a weather source in doubt); the park, a park switch check and faults can't be overridden on an Open. The override stays in effect while the roof opens and ends when the roof reads closed, a close is accepted, or a refusal it doesn't cover appears (`roof.override` shows it, `roof.openOverridable` what may be overridden now). A body that isn't JSON of that shape (at most 256 bytes) or names anything else is refused with `400` and `error`, and nothing happens.


### `POST /api/roof/close`

Close. Refused (`mountNotParked`) unless the mount is confirmed parked: N.I.N.A. reports it at park and every fitted park switch reads closed, as the [safety inputs](../user-guide/safety-inputs.md) decide. During the close the park is checked every 20 ms; if it is lost the motor stops with a `parkInterlock` fault. The weather never blocks a close. If only the park refuses it and N.I.N.A. can be asked, the close waits for the park as an open does (`202`; see above). An optional body `{"override": ["mountNotParked"]}` is a person's confirmed close without the park, for that close only: the park isn't re-checked while it closes, and a `parkInterlock` fault can't come from it. A bad body is refused with `400`.

A close always wins, as long as it would run (past the park check and the motion checks, and not already closed or closing): it cancels a scheduled restart (except the one that remounts a rewritten web UI) and stops an upload in progress (that upload fails with `409` "Upload stopped: the roof is closing"). Once a restart has passed its last check, open and close are refused (`restarting`, "The device is restarting") for the moment before the reset. If an upload, a settings save, a WiFi change or scan, or an MQTT connect is still finishing, the close is accepted as waiting and starts as soon as it is done, within a few seconds (`roof.closePending` in the status); stop or open cancels it:

```json
{ "success": true, "state": "open", "pending": true, "message": "Close starts once this has stopped: An upload is being written" }
```

### `POST /api/roof/stop`

Stop at once, keeping the position estimate (`stopped`). Always accepted.

### `POST /api/roof/clear-fault`

Clear a latched fault after fixing its cause. The fault is removed from storage too; the position is known again only at a magnet. Accepted with no fault.

### `POST /api/roof/release-hold`

Spec 026: a person at the roof ends the automatic close's stand-down (`heldByPerson` after a Stop, `halted` after a close that took too long) before its re-arm spell is over. Body `{ "attended": true }` (`400` without it). Refused (`409`, with `error`) when there is no stand-down, or while the weather check isn't safe or daylight wants the roof closed. The next step of the sequence checks again and applies it. It never moves the roof: it arms the automatic close again, so unsafe weather afterwards starts a new attempt. See [the automatic close](../user-guide/close-on-unsafe.md#release-hold).

### `GET /api/roof/inputs`

The weather sources, the park and N.I.N.A. as the device last evaluated them (once a second): the weather verdict with each SQMeter source's state and reason, the park result, and N.I.N.A.'s last mount reading. The [setup wizard](../user-guide/setup-wizard.md)'s **Test** reads it. Open (no password), like `/api/status`: it returns readings and the device's texts, no address or secret, and sends nothing to SQMeter or N.I.N.A. In the rare moment the device is updating the readings for longer than 50 ms it answers `503` (`{"error": "Inputs not available yet: try again"}`).

```json
{
  "weather": {
    "verdict": "safe",
    "text": "safe",
    "http": { "enabled": true, "state": "good", "generation": 1, "ageMs": 2100, "limitMs": 30000, "safe": true, "flags": 0, "reasons": "", "text": "" },
    "mqtt": { "enabled": false, "state": "off", "generation": 0, "ageMs": 0, "limitMs": 150000, "safe": false, "flags": 0, "reasons": "", "text": "" }
  },
  "park": { "parked": true, "text": "parked" },
  "nina": { "state": "good", "generation": 1, "connected": true, "atPark": true, "slewing": false, "ageMs": 900, "text": "" }
}
```

| Field | |
|---|---|
| `weather.verdict`, `weather.text` | `safe`, `unsafe` or `unknown`, and the same text as `roof.weatherText` in the status |
| `weather.http`, `weather.mqtt` | Each SQMeter source: `state` (`off`, `waiting`, `good`, `stale`, `unreachable`, `unreadable`, `offline`), `ageMs` since its last good reply, the stale `limitMs`, SQMeter's `safe` and `flags` (its `reasonFlags`) from that reply, `reasons` (SQMeter's own reasons text from that reply, `""` when it is safe or sent none; without it `text` words the flags: "cloud cover", "rain"), and the reason `text` ("" when off, or good and safe) |
| `park.parked`, `park.text` | The park rule's answer, as `roof.parkConfirmed` and `roof.parkText` |
| `nina.state` | `notConfigured`, `waiting`, `good`, `unreachable`, `unreadable` or `apiError`; `connected`, `atPark` and `slewing` only mean something when it is `good`; `ageMs` since the reading; `text` the failure |
| `…generation` | Goes up when that source's settings change (its record starts again), so a client can tell a reading of the new settings from an old one |

---

### `POST /api/roof/calibration/start`

Starts an attended [calibration run](../user-guide/calibration-run.md) (spec 016). Login and the `X-RoofRoof: 1` header, like every roof command. Body:

```json
{ "attended": true, "at": "closed", "travelMm": 1850, "topPass": false }
```

`attended` must be `true` (the person is at the roof and can see it); `at` is where the roof is now (`closed`, `open` or `partWay`), checked against the Hall reading; `travelMm` the travel between the magnets' trigger points, measured with a tape (100-20000); `topPass` adds an open and a close at the top duty. `200 {"success": true, "state": "running"}`; `400` for a body of another shape; `409` with `reason` and `error` when refused: `notAttended`, `calibrating`, `closeSequence` (the automatic close isn't idle), `faultLatched`, `moving`, `closeWaiting`, `settingsPending`, `weatherNotSafe`, `daylight`, `mountNotParked`, `parkSwitchCheck` (an open leg), `busy`, `hotspot`, `restarting`, `invalid`, `hallDisagrees`, `notStarted`, or the motion logic's refusals. Nothing is saved by the run.

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
      "speedMmPerSecond": 16.4, "ampsMean": null, "ampsPeak": null, "supplyMinVolts": 12.3 }
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
    "slowSpeedMmPerSecond": { "old": 0, "new": 16.4 },
    "slowSpeedDutyPercent": { "old": 0, "new": 40 },
    "timeoutMarginSeconds": { "old": 15, "new": 10 },
    "moveLimitSeconds": { "old": 67, "new": 59 },
    "slowLimitSeconds": { "old": 137, "new": 123 },
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
| `legs[]` | Each measured leg: time driven, duty, the level it ended on, its average speed (travel ÷ time), and with current sense the mean and peak running current; the lowest supply |
| `hall` | The in-between reading (middle half of each leg) with its noise (standard deviation), and the reading at rest at each end |
| `proposal` | Only when `done`: the value at the start (`old`) and the proposed one (`new`); `null` where withheld, with `speedProblem` (`legsDisagree`, `legTooShort`, `outOfRange`) or `hallProblem` (`samePole`, `weakEnd`, `noRoom`); `warnings[]` (`directionsDiffer`, `currentNearLimit`, `fullSpeedEstimated` without a top-duty pass, `slowLegsLong` when a slow leg used over 75 % of the limit). `slowSpeedMmPerSecond` is the slow legs' average speed; `moveLimitSeconds` and `slowLimitSeconds` aren't settings: the hard timeout of a full open or close, and of a move from an unknown position at the slow duty, with the saved and the proposed settings |

The status document's `roof.calibrating` is `true` for the whole run, pauses included.

---

### `POST /api/roof/jog`

Hold-to-run: a person at the roof moves it at the slow duty while they hold a button ([Teach the ends](../user-guide/calibration-run.md#teach-the-ends), spec 019). Login and the `X-RoofRoof: 1` header. Start a jog:

```json
{ "direction": "open", "attended": true }
```

`200 {"success": true, "state": "opening", "id": 7, "holdMs": 500, "keepAliveMs": 150, "limitMs": 30000}`. While the button is held, send the same body with `"id": 7` every `keepAliveMs`; let go with `{"id": 7, "release": true}` (nothing else needed), which stops that jog only. The device stops the jog by itself `holdMs` after the last keep-alive reached it, by its own control steps (a closed tab or a lost link stops it), at the first magnet after leaving the one it started on, and after 30 s with the `timeout` fault (the page releases the button a second before). Every motion fault and the drive budget apply.

Spec 033: a person's jog runs **blind** (on time alone at the slow duty, never stopping at a magnet, still only while renewed and at most the jog's limit, which then is not a fault; a supply-sag stall and every fault still checked stop it) when the Hall rail fault is the latched fault (or the Hall reads at a rail or not settled), or when the start carries `"ignoreMagnets": true` to override the end magnets. After an override, a magnet read at rest doesn't place the roof until a move reaches one or new roof settings are saved. Any other latched fault refuses both. See [Nudging without the Hall sensor](../user-guide/roof-motion.md#nudging-without-the-hall-sensor).

Both directions need the weather safe, daylight not wanting the roof closed and the mount confirmed parked, because the motor direction setting may still be wrong on a roof being taught; the park interlock watches every jog, and a jog stops when the weather or daylight turns. `409` with `reason` and `error` when refused: `notAttended`, `calibrating`, `closeSequence`, `faultLatched`, `moving`, `closeWaiting`, `settingsPending`, `weatherNotSafe`, `daylight`, `mountNotParked`, `busy`, `hotspot`, `restarting`, `notStarted`, or the motion logic's refusals. A renewal or release of a jog that has ended is refused with `jogEnded` and never starts a new one. [`POST /api/roof/stop`](#post-apiroofstop) stops a jog too. While a jog runs, open and close from anyone are refused with `jogging`; the automatic close stops the jog and then makes its usual checks.

### `POST /api/roof/teach/mark`

`{"at": "closed"}` (or `open`, `partWay`): records the Hall reading over the last second as that place. Login and `X-RoofRoof: 1`. `409` with `notAtRest` (moving, jogging, or not still for a second yet), `hallNotSettled` (unsettled or at a rail) or `calibrating`. A new mark replaces the old one for the same place.

### `POST /api/roof/teach/confirm`

Saves the proposal of `GET /api/roof/teach` through the settings save. Send the three values shown:

```json
{ "hallHighEnd": "open", "hallHighCounts": 2725, "hallLowCounts": 1381 }
```

`200 {"success": true}`; `409` with `noProposal` or `proposalChanged`, or the settings save's own refusal while the roof moves; `400` if the result isn't valid. Only those three settings change; the marks are then cleared, and once the settings are applied the roof's position follows the magnet it sits on.

### `POST /api/roof/teach/discard`

Drops the marks and the proposal; changes no setting. Login and `X-RoofRoof: 1`.

### `GET /api/roof/teach`

The jog under way, the reading window, the marks and the proposal. Open, like `/api/status`; kept in memory only.

```json
{
  "jog": { "active": false, "id": 7, "direction": null, "elapsedMs": 0, "limitMs": 30000, "holdMs": 500, "keepAliveMs": 150 },
  "rest": { "ready": true, "counts": 3399.5 },
  "marks": {
    "closed": { "count": 50, "mean": 712.4, "min": 709, "max": 716, "noise": 1.6 },
    "open": { "count": 50, "mean": 3399.6, "min": 3397, "max": 3402, "noise": 1.4 },
    "partWay": { "count": 50, "mean": 2050.1, "min": 2047, "max": 2053, "noise": 1.2 }
  },
  "proposal": { "ok": true, "hallHighEnd": "open", "hallHighCounts": 2725, "hallLowCounts": 1381 }
}
```

| Field | |
|---|---|
| `jog` | `active`, the `id` of the last jog started, its `direction` (`open`, `close`, or `null`), the time it has driven and its `limitMs`, and the keep-alive deadline (`holdMs`) and interval (`keepAliveMs`) |
| `rest` | `ready` once the roof has been still for a second, with the mean reading over it (`counts`) |
| `marks` | Each place's reading at rest (`null` until marked): readings taken, mean, lowest, highest and noise (standard deviation) |
| `proposal` | `null` until all three are marked; then the high end and both thresholds, or `ok: false` with `reason` (`samePole`, `weakEnd`, `noRoom`) and its text |

---

## Diagnostics

The [advanced diagnostics](../user-guide/diagnostics.md) routes. They are open (readings only, no password), like `/api/status`, except clearing the log and the crash records and log (spec 029), which need the password when protection is on. With `diag.enabled` off, each read answers `200 {"enabled": false}` (a move by id answers `404`) and records nothing. A value the device can't read is `null`.

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

### `GET /api/diag/crashes`

The last 3 [unexpected restarts](../user-guide/diagnostics.md#crashes) (a crash, a watchdog or a brown-out), newest first, as plain text (`text/plain`), one fact per line. Always on, whatever `diag.enabled` says; needs the password when protection is on (`401` without it). It changes nothing.

```text
This start: panic (4), boot 3
Running: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Records: 2 (the last 3 are kept)

Record 2: panic
Time: 2026-11-25 02:14:07 UTC, 5231 s after start
Reset reason: panic (4)
Firmware: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Reason: LoadProhibited
Task: async_tcp, core 1, stack left 1904 B
Flash write: in progress
Backtrace: 0x400d5f2c 0x400d5f2c 0x400d61a0 0x40089e1d
Memory: free 61224 B, lowest 41020 B, largest block 31732 B
Stack left: loop 5980, roof 3416, async_tcp 6640, safetyPoll 2120, alerts 3876 B
Roof: closed
Log before the restart:
  5229.120s INFO  [Main] Settings saved

Record 1: brownout
Time: unknown (clock not set), 812 s after start
Reset reason: brownout (9)
Firmware: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Reason: not captured
```

| Line | Meaning |
|---|---|
| `This start`, `Running` | Why this start happened (SQMeter's reset reason names: `poweron`, `software`, `panic`, `watchdog`, `brownout`, ...) and the code, the boot count, and the running firmware and build |
| `Record <seq>: <kind>` | `panic`, `watchdog` or `brownout`; the sequence keeps rising across restarts |
| `Time` | When it crashed (UTC), or "unknown" when the clock wasn't set; and the seconds since that start |
| `Firmware` | The version and build that crashed: the build is the first 16 hex digits of the SHA-256 of that build's `firmware.elf`, which [`tools/decode_crash.py`](../development/crash-decoding.md) checks |
| `Reason` | The panic's own words, or "not captured" for a brown-out or a reset with no panic (then the task, backtrace and flash lines are left out) |
| `Task` | The task that was running, its core, and the stack it had left (near 0: it overflowed; `?` unknown) |
| `Stuck tasks` | For a task watchdog: the tasks that didn't check in |
| `Flash write` | A flash write (a settings save, a fault record, the kept move summaries, an update) was in progress at the panic |
| `Backtrace` | The PC, then up to 16 frames; `[corrupt]` and `[continues]` when a frame was bad or there were more |
| `Memory`, `Stack left`, `Roof` | Free memory and the stack left in five tasks (bytes, `?` unknown) and the roof's state from the last second before the restart; left out when that note didn't survive |
| `Log before the restart` | The last 8 log lines before it |

### `GET /api/diag/log`

The device's last 50 log lines as plain text, oldest first, including the lines from before a crash or a software restart (not a power cut). Needs the password when protection is on.

```text
#2 5229.120s INFO  [Main] Settings saved
#3 0.412s INFO  [Main] === RoofRoof Starting ===
```

`#` is the boot (counted since the log was last emptied by a power cut), then the time since that start, the level and up to 71 characters of text.

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

Add `event=<event>&level=<1-4>&sound=<pushover sound>` to send a sample of that event (title "Test: ...") at that level and sound instead. Events: `mount_not_parked`, `roof_not_closed` (critical: sent at level 3 or above), `move_fault`, `supply_low`, `unsafe_closing`, `roof_closed`, `daylight_open`, `daylight_closed`, `daylight_unknown`, `safety_lost`, `nina_lost`, `client_lost`, `client_back`, `client_disconnected`. `title` and `message` (up to 80 / 240 characters) try out custom wording with `{variables}`.

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
