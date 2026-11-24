# Contract: Park Source and SQMeter Secrets

## Settings (`GET`/`POST /api/config`)

```json
{"park": {"source": "switches", "switches": "two", "nina": {"url": "", "api": "v2"},
          "timeoutSeconds": 240, "requestPark": true}}
```

- `source` absent with `switches` present: `none` → `nina`, `one`/`two` → `ninaAndSwitches`.
- Refused (400): `nina` with a switch; `switches`/`ninaAndSwitches` with none; `testBox.park` with `switches`.
- A secret field (`mqtt.password`, `alerts.ntfy.token`, `alerts.pushover.userKey`,
  `alerts.pushover.appToken`) set to `"(from SQMeter)"` takes the value the device received in the
  last copy (held 30 min), only for the server it came with: MQTT broker, port and user name; ntfy
  server. Otherwise 400 with why. The placeholder is never stored or returned.

## Copy (`POST /api/sqmeter/site`), additions

```json
{"mqtt": {"enabled": true, "...": "..."},
 "ntfy": {"enabled": true, "...": "..."},
 "pushover": {"enabled": false, "userKeySet": true, "appTokenSet": true},
 "secrets": {"problem": null, "http": true,
             "mqttPassword": "received", "ntfyToken": "none", "pushover": "same"}}
```

`problem`: `notSaved` (the copy's address isn't the saved one), `notSupported` (404), `sharingOff`,
`noWebPassword` (403), `loginNeeded`, `loginRefused` (401), `tooManyLogins` (429: SQMeter locked the
endpoint for a minute after five failed logins), `unreachable`, `unreadable`. When SQMeter's
`GET /api/config` has `sharing.secrets` false, nothing is asked and the problem is `sharingOff`.

## SQMeter's `GET /api/secrets` (SQMeter PR #191)

Checked in this order: sharing off (403 "Password sharing is off in SQMeter"), no web password (403
"Set a web password in SQMeter before sharing passwords"), five failed logins in a row (429 "Too
many failed logins; try again in a minute", Retry-After, for 60 s, device-wide), a bad login (401),
else 200 with all four keys ("" when not set). Plain HTTP only. The sync never asks again after a
401 until SQMeter's saved login changes, and after a 429 waits for the next settings read (10 min).

## Status

`roof.parkSwitches`: `{"fitted", "switch1", "switch2", "switchesOnly", "openings": [0, 3], "check": "Park switch 2 hasn't opened ..."}`.
`sync.secrets`: null, `"received"` or a problem name.

## Roof commands

Open refused (`parkSwitchCheck`) while `roof.parkSwitches.check` is set (switches only).
