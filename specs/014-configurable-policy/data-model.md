# Data Model: Configurable Policies

## Config (stored, `GET/POST /api/config`)

```json
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
"ui": { "confirmOpenClose": false },
"mqtt": { "...": "...", "changeMinSeconds": 1 },
"daylight": { "...": "...", "bandDeg": 0.5 }
```

A missing section or field keeps its default. `notStarted` other than `halt` or `retry` is
refused. Ranges: [contracts/settings.md](contracts/settings.md).

## `CloseSeq::Policy` (lib/CloseSequenceLogic/include/ClosePolicy.h)

| Field | Default | Clamp | Replaces |
|---|---|---|---|
| `parkRequestGapMs` | 30,000 | 10,000–120,000 | `PARK_REQUEST_GAP_MS` |
| `parkSettleMs` | 4,000 | 4,000–30,000 | `PARK_SETTLE_MS` |
| `retryAfterMs` | 300,000 | 60,000–1,800,000 | `RETRY_AFTER_MS` |
| `rearmSafeMs` | 600,000 | 300,000–3,600,000 | `REARM_SAFE_MS` |
| `closeStartMs` | 30,000 | 25,000–120,000 | `CLOSE_START_MS` |
| `closeGuardSlackMs` | 30,000 | 10,000–120,000 | `CLOSE_GUARD_SLACK_MS` |
| `remindMs` | 1,800,000 | 600,000–7,200,000 | `REMIND_MS` (and `ALERT_RETRY_MAX_MS`) |
| `retryNotStarted` | false | — | spec 005 item 11g |

`policyFrom(const Config &)` converts the stored minutes and seconds; `clamp(Policy)` applies the
table. `Sequence::setPolicy` stores the clamped policy and it applies from the next step.

## `CloseSeq::Cause::NotStarted`

After `RoofNotStarted`. Event `CloseFailed`; action `Stop`; phase `RetryWait`; `limitMs` is the
close-start time.

## `CloseSeq::Decision`

Adds `retryMs` and `rearmMs` (the policy's, for the wording).

## `RoofIo::CommandContext` / `CommandCheck`

`CommandContext::remoteStopsClose` (default false). `CommandCheck::standsDown`: the stop counts as
a person's (only ever true for a remote stop accepted because of the setting).

## `RoofIo::Mqtt::StateSchedule`

`setChangeMinMs(ms)` (default `CHANGE_MIN_MS`, clamped 1,000–60,000), used at rest only.

## `Daylight::Settings`

Adds `bandDeg` (default `BAND_DEG`, clamped 0.1–2.0).
