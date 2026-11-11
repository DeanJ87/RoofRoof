# Research: Safety Inputs

Accessed 2026-11-11. Two external contracts (SQMeter's safety outputs and N.I.N.A.'s Advanced
API) and the design decisions that follow from them.

## R1. SQMeter's safety contract

Read from the SQMeter repository (`~/Projects/SQMeter`, `main`); file and line references are to
that checkout.

### Sources

- `specs/013-data-interfaces/contracts/rest-responses.md` and `mqtt-topics.md`. Each says its
  source of truth is the generated schema in `specs/016-demo-device-emulation/contracts/schemas/`.
- `specs/016-demo-device-emulation/contracts/schemas/safety.schema.json` (lines 5-51).
- `src/WebServerApi.cpp:64` (route), `:143-150` (handler); `lib/DeviceCore/src/DeviceCore.cpp:517-534`
  (`Core::writeSafety`); `lib/AlpacaLogic/include/SafetyEvaluator.h:106-125` (reason bits);
  `src/WebServerAlerts.cpp:318-339` (`publishMqttSafety`); `src/MQTTClient.cpp:136-193`
  (availability and last will); `include/WebServer.h:206, 265` (intervals);
  `docs/api/rest.md:462-487`; `tools/api/examples/safety.json`.

### `GET /api/safety`

- Always HTTP 200, `application/json`, no auth (`"security": []` in SQMeter's OpenAPI), no cache
  headers.
- Nine fields, all required, `additionalProperties: false`:

  | Field | Type | Meaning |
  |---|---|---|
  | `safe` | bool | The reported verdict (after SQMeter's own safe delay); the same as Alpaca `IsSafe`. Never null. |
  | `rawSafe` | bool | The rule result now, before the safe delay. |
  | `alpacaEnabled` | bool | A config flag; doesn't gate evaluation. |
  | `reasonFlags` | uint32 | Bitmask below; 0 when safe. |
  | `reasons` | string[] | English text, may be reworded between releases. SQMeter's docs say to key on `safe` and `reasonFlags`, not this. |
  | `secondsUntilSafe` | number | Safe-delay countdown. |
  | `rulesNotInEffect` | string[] | Rules switched on but ignored. |
  | `evaluatedAgeMs` | number | Milliseconds since the last evaluation (0 also means "never evaluated", for at most about 1 s after start). |
  | `changedAgeMs` | number | Milliseconds since `safe` last changed. |

- Example (verbatim, `tools/api/examples/safety.json`):

  ```json
  {"safe": true, "rawSafe": true, "alpacaEnabled": true, "reasonFlags": 0, "reasons": [],
   "secondsUntilSafe": 0, "rulesNotInEffect": [], "evaluatedAgeMs": 0, "changedAgeMs": 2005}
  ```

  Rain (verbatim, `docs/api/rest.md:467-477`):

  ```json
  {"safe": false, "rawSafe": false, "alpacaEnabled": true, "reasonFlags": 512,
   "reasons": ["Rain detected"], "secondsUntilSafe": 0, "rulesNotInEffect": [],
   "evaluatedAgeMs": 412, "changedAgeMs": 1260000}
  ```

- SQMeter has no "unknown": it starts unsafe (`isSafe = false`), sets bit 1 "no data yet" and
  bit 2 "stale", and re-evaluates every 1000 ms.

### `reasonFlags` (verbatim from `SafetyEvaluator.h`)

| Bit | Name | | Bit | Name |
|---|---|---|---|---|
| 0 | ManualOverride | | 9 | **Rain** |
| 1 | NoData | | 10 | **RainSensorFault** |
| 2 | StaleData | | 11 | Wind |
| 3 | SensorFault | | 12 | WindGust |
| 4 | CloudCover | | 13 | WindSensorFault |
| 5 | SkyBright | | 14 | BatteryLow |
| 6 | Humidity | | 15 | BatteryData |
| 7 | Dewpoint | | 16 | Shutdown |
| 8 | EnvironmentFault | | | |

Bit 9 is set when the "unsafe while raining" rule is on and it is raining (including SQMeter's
post-rain hold-off); bit 10 when the rain sensor is required and unhealthy.

### MQTT

From `contracts/mqtt-topics.md` (verbatim): "`<base>` is the configured base topic (default
`sqmeter`). Booleans are `1`/`0`; availability uses Home Assistant's `online`/`offline`."

| Topic | Retained | Payload | When |
|---|---|---|---|
| `<base>/availability` | yes | `online` / `offline` (last will, QoS 1) | connect / disconnect |
| `<base>/safe` | yes | `1` safe / `0` unsafe | on change, on reconnect, and every 60 s (`MQTT_SAFETY_REPUBLISH_MS = 60000`) |
| `<base>/safety` | yes | the `GET /api/safety` object | same as `safe` |

- Publishes are QoS 0 (PubSubClient). A change in `reasonFlags` alone waits for the next 60 s
  refresh; a change in `safe` goes out within about 1 s.
- Nothing is published before SQMeter's first evaluation.
- Safety topics are published only while SQMeter's "safety" publish group is on (default on).

### Decisions

- **D1. Read only `safe`, `reasonFlags` and `evaluatedAgeMs`.** They are the stable contract.
  `safe` and `reasonFlags` must be present with the right type, or the reply is unreadable.
  `evaluatedAgeMs` is optional (an older SQMeter may not send it); when present, it adds to the
  reply's age. Unsafe if `safe` is false **or** `reasonFlags` is non-zero.
  *Alternatives:* key on `reasons` text (rejected: SQMeter says it may change); use `rawSafe`
  (rejected: it skips SQMeter's safe delay, which the owner set for a reason).
- **D2. HTTP poll 5 s, timeout 3 s, stale default 30 s (setting, 10-600 s).** SQMeter evaluates
  every second; 30 s allows several missed polls on a weak link before calling it stale, matching
  SQMeter's own default stale time for its sensors.
- **D3. MQTT: subscribe to `safe`, `safety` and `availability`; stale after 150 s.** SQMeter
  republishes every 60 s, so 150 s is two missed refreshes plus margin. The `safety` JSON carries
  `reasonFlags`, so rain is recognised over MQTT too. The source is trusted only after a retained
  or live `online`; `offline` (the last will) makes it unsafe at once, which also covers a
  retained `safe` left on the broker by a dead SQMeter.
  *Alternative:* a configurable MQTT stale time (rejected: SQMeter's 60 s is a constant, so a
  setting adds a way to get it wrong and nothing else).
- **D4. `safe` payloads `1`/`0` only; `availability` payloads `online`/`offline` only.** Anything
  else is unreadable. Whitespace is not trimmed (SQMeter never sends it).

## R2. N.I.N.A. Advanced API (christian-photo/ninaAPI)

### Sources

- Repository: <https://github.com/christian-photo/ninaAPI>
- README (version scheme): <https://raw.githubusercontent.com/christian-photo/ninaAPI/dev/README.md>
- v2 OpenAPI spec: <https://raw.githubusercontent.com/christian-photo/ninaAPI/dev/ninaAPI/api_spec.yaml>
  (`info.version` 2.2.15; mount park at lines 3698-3756, mount info at 3411-3444, `MountInfo`
  schema at 9509+)
- v2 mount controller: <https://raw.githubusercontent.com/christian-photo/ninaAPI/dev/ninaAPI/WebService/V2/Equipment/Mount.cs>
  (`MountPark()` lines 205-238, info lines 98-115)
- Default settings: <https://raw.githubusercontent.com/christian-photo/ninaAPI/dev/ninaAPI/Properties/Settings.settings>
- Changelog: <https://raw.githubusercontent.com/christian-photo/ninaAPI/dev/Changelog.md>
- Releases: <https://api.github.com/repos/christian-photo/ninaAPI/releases>
- v3 spec (beta): <https://raw.githubusercontent.com/christian-photo/ninaAPI/v3/doc/api_spec_v3.yaml>
- v3 mount controller: <https://raw.githubusercontent.com/christian-photo/ninaAPI/v3/ninaAPI/WebService/V3/Equipment/Mount/MountController.cs>
- Docs site (JavaScript viewer of the same specs): <https://christian-photo.github.io/github-page/projects/ninaAPI/v2/doc/api>

### Versions

- v2 line: latest stable **2.2.15.3** (2026-11-07). The plugin's first version number is the API
  major version; 2.x serves `/v2/api`.
- v3 line: beta only, **3.0.0.5-b.6** (2026-11-07), serves `/v3/api`.
- Default port **1888** (user-configurable). Server in the v2 spec: `http://localhost:1888/v2/api`.
- 2.2.15.0's changelog: "Fix an issue where the mount info endpoint would fail" — so 2.2.15.0 or
  newer is required.

### v2 envelope

Every v2 reply is `{"Response": ..., "Error": "...", "StatusCode": n, "Success": bool, "Type": "API"}`.
Success has `Error: ""`; failure has `Response: ""`, `Success: false` and an `Error` text. Mount
endpoints use 409 for "Mount not connected", 400 for bad input, 500 "Unknown error". No
authentication in v2 (no `securitySchemes`).

### `GET /v2/api/equipment/mount/info`

`Response` is NINA's `TelescopeInfo` plus `TrackingMode`. The fields RoofRoof needs, exact names:
`Connected` (bool, required by the schema), `AtPark` (bool), `Slewing` (bool). Others include
`TrackingEnabled`, `CanPark`, `AtHome`, `Name`, `DisplayName`, `DeviceId`, coordinates and rates.
On an exception: the 500 envelope.

### `GET /v2/api/equipment/mount/park`

No parameters. Behaviour from `Mount.cs`:

- not connected → 409 `{"Response":"","Error":"Mount not connected","StatusCode":409,"Success":false,"Type":"API"}`
- already parked → 200, `Response: "Mount already parked"`
- otherwise stops any slew, starts `ParkTelescope` **without awaiting it**, and returns 200,
  `Response: "Parking"`.

So the park is asynchronous: the reply only means "started". Completion is seen by polling mount
info for `AtPark: true`. Park doesn't check `CanPark`; a park that fails inside N.I.N.A. after
"Parking" shows only as `AtPark` never becoming true.

### v3 (for later)

- Base `/v3/api`, `http` or `https`; optional HTTP Basic auth (global `basicAuth` scheme, only
  enforced if enabled in the plugin).
- Info: `GET /v3/api/equipment/mount` returns the bare `MountInfo` object (no envelope), same
  field names.
- Park: `POST /v3/api/equipment/mount/park` → 202 `{"ProcessId": "<guid>", "Status": "Running"}`;
  409 for not connected, "Mount already parked" (an error in v3, unlike v2) and "Mount cannot
  park".
- Errors: `{"Error": "<HTTP reason>", "Message": "..."}`.

### Not verified

- No live reply was captured from a running plugin; the fixtures in `test/test_nina/` are built
  from the spec and the controller source, and are marked as such.
- Whether v2's HTTP status line always equals the body's `StatusCode` (the parser therefore
  trusts neither alone: it needs HTTP 200 **and** `Success: true`).
- The values of the other info fields when the mount is disconnected (only `Connected` is used
  then).

### Decisions

- **D5. An interface (`Nina::Api`) that builds requests and parses replies; `Nina::ApiV2` now.**
  A request is a method, a path and optional Basic-auth credentials, so v3's POST, path and auth
  fit without changing callers. Parsing returns a typed result, never throws.
  *Alternative:* a client class doing HTTP itself (rejected: HTTP can't be native-tested, and
  STRUCT-01 keeps networking out of `lib/`).
- **D6. Info is good only with HTTP 200, a JSON object, `Success: true` and a `Response` object
  with a boolean `Connected`; when `Connected` is true, `AtPark` and `Slewing` must be booleans
  too.** A disconnected mount is a good reading with `connected = false` (it is not parked).
- **D7. Park: "Parking" or "Mount already parked" with HTTP 200 and `Success: true` = accepted;
  `Success: false` = refused with the `Error` text; anything else = unreadable.** Accepted never
  means parked; only the park rule says that.
- **D8. Poll mount info every 2 s, 3 s timeout, fresh for 10 s.** Short enough that the park
  check at the start of a close sees a recent state; the park endpoint is called only on request.

## R3. Firmware integration

- **D9. One background task ("SafetyPoll") for both HTTP pollers**, 6 KB stack, priority 1, on
  core 0 (the Wi-Fi core; the async web server is on core 1). Plain `http://` only, so no TLS and
  no contention with the alert task's TLS session (constitution VI). It runs `HTTPClient` with a
  `DualStackClient` as `AlertDispatcher` does (IPv6 literals work). Readings go into a
  mutex-guarded snapshot with 64-bit millisecond timestamps (`esp_timer`, never wraps); the main
  loop reads the snapshot. Settings saved on the web server's task are handed to the loop, which
  applies them.
  *Alternative:* polling from the main loop (rejected: a 3 s timeout would stall the loop and the
  watchdog margin).
- **D10. MQTT messages arrive on the main loop** (PubSubClient's callback runs inside
  `handle()`). `MQTTClient` gains a list of absolute-topic subscriptions, re-subscribed on every
  connect; the existing single command subscription stays. Recorded in `heritage.md` (changed,
  upstream candidate: no).
- **D11. The verdict and park result are computed in the main loop once a second** from the
  snapshot, the MQTT state and the input values that spec 003 will provide (until then: the rain
  contact and switches are "unreadable" when fitted, which is fail-safe), and logged on change.
- **D12. Settings in a new `safety` section and a `park` section** of the main config document
  (about 300 bytes at the longest URLs, well inside `MAX_PERSISTED_JSON_BYTES`).
