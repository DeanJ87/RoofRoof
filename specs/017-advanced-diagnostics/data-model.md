# Data Model: Advanced Diagnostics

All types are in `lib/DiagLogic` (namespace `SQM::Diag`) unless stated. Pure: time and readings
come in as values.

## Settings (`lib/ConfigModel`, `DiagConfig`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `diag.enabled` | bool | false | Advanced diagnostics: the live document, the stream, the move log |
| `diag.keepSummaries` | bool | false | Keep the last 32 move summaries across restarts (only with `enabled`) |
| `diag.mqtt` | bool | false | Publish `<base>/diag` and `<base>/diag/move` (only with `enabled` and MQTT on) |

Missing keys load as false (stored settings from earlier releases). No restart; no motion setting.

## Limits

| Name | Value | Why |
|---|---|---|
| `MAX_MOVES` | 5 | moves held with their profiles (the one in progress counts) |
| `MAX_POINTS` | 120 | profile points per move |
| `FIRST_INTERVAL_MS` | 250 | a profile point's first interval (4 a second) |
| `MAX_SUMMARIES` | 32 | summaries in the list and the kept file |
| `REST_BLOCK_MS` | 1000 | the block of rest readings the supply at rest is taken from |
| `STALL_READINGS` | 13 | driven current readings averaged for the stall current (260 ms) |
| `STREAM_MOVING_MS` / `STREAM_REST_MS` | 250 / 2000 | the stream's interval |
| `MQTT_LIVE_MS` | 60000 | the live document's MQTT interval at rest |
| `KEPT_WRITE_MIN_MS` | 300000 | the least time between two writes of the kept file |

`static_assert(sizeof(MoveLog) + sizeof(SummaryList) <= 8192)`.

## Sample (one control step, built by the step)

| Field | Type | Source |
|---|---|---|
| `nowMs` | uint32 | the step's clock |
| `status` | `Roof::Status` | the controller after `tick()` |
| `output` | `Roof::Output` | the controller's output (command, duty 0-255) |
| `supplyVolts` | float | the step's reading; NaN unreadable |
| `motorAmps` | float | the running current (`RoofIo::runningAmps`); NaN without current sense |
| `origin` | `Roof::Origin` | of the last accepted open or close |
| `slowDuty`, `topDuty` | uint8 | the settings' duties as 0-255 |

## Point (8 bytes)

| Field | Type | Meaning |
|---|---|---|
| `ampsAvg` | uint16 | centiamps; `NONE` (0xFFFF) when no reading |
| `ampsMax` | uint16 | centiamps; `NONE` |
| `voltsMin` | uint16 | centivolts; `NONE` |
| `duty` | uint8 | average duty 0-255 |

Accessors give physical units (`pointAmpsAvg(p)` → float or NaN, and so on).

## Summary

| Field | Type | Notes |
|---|---|---|
| `id` | uint32 | from 1; continues after the highest kept id |
| `active` | bool | the move is still in progress |
| `kept` | bool | loaded from the kept file (no uptime, no profile) |
| `startMs`, `endMs` | uint32 | uptime; `endMs` is the last step so far while active |
| `startEpoch`, `endEpoch` | uint32 | Unix seconds when the clock was set; 0 otherwise (filled in when the list takes it) |
| `drivenMs` | uint32 | time the driver was on |
| `origin` | `Roof::Origin` | person, automatic, client, mqtt |
| `target` | `Roof::End` | |
| `endState` | `Roof::State` | |
| `result` | `Result` | `arrived` (target magnet), `stopped`, `fault` |
| `fault` | `Roof::Fault` | kind, measured, limit (kind None unless `fault`) |
| `fromUnknown` | bool | |
| `peakAmps`, `averageAmps`, `stallAmps` | float | NaN when none |
| `restVolts`, `minVolts` | float | NaN when none; sag = rest − min |
| `hallStart`, `hallEnd` | uint16 | raw counts |

## Move

`Summary summary; uint32_t intervalMs; uint8_t count; Point points[MAX_POINTS];` (about 1 KB).
Point `i` covers `[i × intervalMs, (i + 1) × intervalMs)` from the move's start; the last may be
shorter.

## MoveLog (the control task's recorder)

- `step(const Sample &)`: under the roof's lock, once per control step.
- `clear()`, `setNextId(uint32_t)`, `nextId()`.
- `finished()`: moves finished since the log was created (the main loop's cue).
- `copy(uint32_t id, Move &) const`, `ids(uint32_t (&)[MAX_MOVES]) const` (newest first),
  `latest(Summary &) const`, `live() const` → `LiveLog {Phase phase; float restVolts; float sagVolts;
  uint32_t moveId; bool moveActive;}`.
- State: the ring of `MAX_MOVES` moves, the point being filled (sums, counts, extremes), the rest
  block, the stall ring, the previous state and duty, `reachedTop`, `slowingSeen`, `primed` (the
  first step after attaching only primes: a move under way isn't recorded).

Transitions: not moving → moving starts a move (a new id; the oldest held move is overwritten);
moving → not moving ends it (result from the end state and fault); opening ↔ closing directly ends
one and starts another.

## SummaryList (main loop)

Up to `MAX_SUMMARIES` summaries, newest first, with `revision` (bumped on every change) and
`add(Summary, bootEpoch)` (fills the epochs when the clock is set), `clear()`, `loadKept(...)`.

## Kept file (`/diag-moves.bin`)

Header 16 bytes: magic `RRDG`, version (1), count, reserved, next id (uint32), CRC-32 of the
records (uint32). Then `count` records of 48 bytes, little-endian, newest first: id, start and end
epoch, duration, driven ms, origin, target, end state, result, fault kind, flags (fromUnknown),
fault measured and limit, peak, average and stall current, rest and lowest supply (floats, NaN
kept as NaN), Hall at start and end. A header or CRC that doesn't match is ignored.

`KeptWrite::due(nowMs, revision, settingOn)`: the setting on, the revision changed since the last
write, and no write in the last `KEPT_WRITE_MIN_MS` (or none yet). The platform check is the
firmware's (`PlatformGuard::FlashWrite`).

## MqttSchedule

`liveDue(nowMs, busy)`: not busy and `MQTT_LIVE_MS` since the last live publish (or none yet).
`summaryDue(busy, newestId, publishedId)`: not busy and a summary newer than the last published.

## LiveView (what the live document shows)

The roof (from `RoofControl::diagLive()` plus the derived power, sag, remaining time and
direction), the control task (period, step times, gap, watchdog limit, margin, oldest beat,
trips), the loop (pass times), memory (free, minimum, largest block, three stack figures), Wi-Fi
RSSI, uptime, reset reason and simulation. Integers that can't be read are carried as
`Diag::UNKNOWN` (-1) and written as null; floats as NaN.
