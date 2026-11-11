# Data Model: Safety Inputs

All types are in `namespace SQM::Safety` (`lib/SafetyInputLogic`) or `SQM::Nina`
(`lib/NinaLogic`). Times are 64-bit milliseconds since start (`esp_timer`), passed in; they
never wrap, and a timestamp after `now` counts as age 0.

## Weather side

### `SqmeterReply` (one good SQMeter answer)

| Field | Type | From |
|---|---|---|
| `safe` | bool | `safe` (required, bool) |
| `reasonFlags` | uint32 | `reasonFlags` (required, non-negative integer) |
| `evaluatedAgeMs` | uint32 | `evaluatedAgeMs` (optional, non-negative integer; 0 if absent) |

`parseSafetyJson(body, out)` → `ReplyError` {None, NotJson, NotObject, MissingSafe,
MissingReasonFlags, BadEvaluatedAge}. `parseSafePayload("1"/"0")` and
`parseAvailability("online"/"offline")` → optional values.

`SqmeterReply::unsafe()` is `!safe || reasonFlags != 0`.

### `SourceRecord` (kept by the firmware per SQMeter source)

| Field | Meaning |
|---|---|
| `enabled` | from settings |
| `haveGood`, `goodAtMs`, `last` | the last good reply and when it arrived |
| `failure`, `failureAtMs`, `detail` | the latest failed attempt: None, Unreachable, Unreadable, HttpStatus; text such as `HTTP 404` or `missing reasonFlags` |
| `availability` | MQTT only: Unknown, Online, Offline |

### `SourceState` (assessment of one source at `now`)

`Off`, `Waiting`, `Good`, `Stale`, `Unreachable`, `Unreadable`, `Offline`.

Rules, in order (`assessSource`):

1. not enabled → Off
2. MQTT availability Offline → Offline
3. a good reply (and, for MQTT, availability Online): its age (time since it arrived plus
   SQMeter's `evaluatedAgeMs`) ≤ the limit → Good (its `last` decides safe/unsafe); older →
   step 5
4. within the start-up window (time since start ≤ the limit) → Waiting
5. the latest failure is newer than the last good reply → Unreachable / Unreadable
6. otherwise → Stale (no good reply for longer than the limit, or none since start)

How answers reach the record (`SqmeterSource.h`): `recordHttpReply` (no reply → Unreachable;
not 200 or not the document → Unreadable; else good) and `applyMqttMessage` (availability,
`safe`, `safety`; the first `safe`/`safety` after `resubscribed()` counts only if unsafe).

A failed attempt never refreshes a source, but doesn't make it unsafe by itself while its last
good reply is still within the limit (one dropped poll on a weak link must not close the roof).

### `WeatherInputs`

`http`, `mqtt` (`SourceRecord`), `rainContact` (NotFitted, Dry, Raining, Unreadable),
`httpLimitMs` (setting × 1000), `mqttLimitMs` (150 000), `startedAtMs`, `nowMs`.

### `WeatherVerdict`

| Field | Meaning |
|---|---|
| `verdict` | Safe, Unsafe, Unknown |
| `reasons` | bit set: Rain, RainSensorFault, WeatherUnsafe, SourceStale, SourceUnreachable, SourceUnreadable, SourceOffline, NoWeatherSource, RainContactUnreadable |
| `sources[3]` | per source (SQMeter HTTP, SQMeter MQTT, rain contact): state, age, limit, SQMeter flags, detail |

Combination (`evaluateWeather`):

- Rain if any SQMeter source's last good reply has bit 9 (even if now stale) or the contact reads
  Raining; RainSensorFault for bit 10.
- WeatherUnsafe if a Good source's reply is unsafe for any other reason.
- Source* reasons for every enabled source in that state; RainContactUnreadable when fitted and
  unreadable; NoWeatherSource when HTTP and MQTT are both off.
- Verdict: Unsafe if any reason is set; else Unknown if any enabled source is Waiting; else Safe.

`describe(verdict)` → one line per problem, e.g. `SQMeter HTTP stale: no good reply for 75 s > 30 s`,
`SQMeter MQTT offline`, `SQMeter says unsafe: reasonFlags 0x200 (rain)`.

## Park side

### `Nina::MountReading`

| Field | Meaning |
|---|---|
| `status` | NotConfigured, Waiting, Good, Unreachable, Unreadable, ApiError |
| `connected`, `atPark`, `slewing` | valid when Good |
| `takenAtMs` | when this reading (good or failed) was taken |
| `detail` | failure text (`connection refused`, `HTTP 500`, `missing AtPark`, N.I.N.A.'s `Error`) |

Each poll replaces the reading: the park rule looks at the latest attempt only.

### `Nina::Api` (interface)

| Member | v2 |
|---|---|
| `mountInfoRequest()` | `{Get, "/v2/api/equipment/mount/info"}` |
| `parkRequest()` | `{Get, "/v2/api/equipment/mount/park"}` |
| `parseMountInfo(status, body, nowMs)` | `MountReading` (see contracts/nina-api-v2.md) |
| `parsePark(status, body)` | `ParkReply` {Accepted, AlreadyParked, Refused, Unreadable; detail} |

`Request` = method, path, username, password (empty in v2; v3's Basic auth).
`makeApi(ApiVersion::V2)` returns the implementation.

### `ParkInputs` → `ParkResult` (`evaluatePark`)

Inputs: `MountReading nina`, `fittedSwitches` (0, 1 or 2), `switch1`, `switch2` (Closed, Open,
Unreadable), `nowMs`, `ninaMaxAgeMs` (10 000).

Result: `parked` (bool), `reasons` (NinaNotConfigured, NinaWaiting, NinaUnreachable,
NinaUnreadable, NinaApiError, NinaStale, MountNotConnected, MountNotAtPark, MountSlewing,
Switch1Open, Switch1Unreadable, Switch2Open, Switch2Unreadable, SourcesDisagree), `disagree`,
`ninaAgeMs`.

`parked` = no reasons. `disagree` = a good, fresh, connected N.I.N.A. reading and at least one
fitted switch, where N.I.N.A. at park and some fitted switch open, or N.I.N.A. not at park and
every fitted switch closed.

## Settings (`Config`)

```text
safety: { http: { enabled: false, url: "" },
          mqtt: { enabled: false, topic: "sqmeter" },
          staleSeconds: 30,                 // 10..600, SQMeter HTTP only
          rainContact: false }              // fitted
park:   { nina: { url: "", api: "v2" },     // url empty = not configured
          switches: "none" }                // none | one | two
```

Validation (device and web UI): `safety.http.url` is required and must be `http://` when HTTP is
on; `park.nina.url` empty or `http://`; both parsed by `Net::parseHttpUrl` (IPv6 literals
allowed); `safety.mqtt.topic` follows the MQTT topic rule and is required when MQTT is on;
`staleSeconds` 10-600; `park.nina.api` only `v2`; `park.switches` one of the three.
