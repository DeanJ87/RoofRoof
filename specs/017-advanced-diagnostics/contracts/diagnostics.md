# Contract: Advanced diagnostics interfaces

All documents are JSON unless stated. Values that can't be read are `null`. With `diag.enabled`
off every endpoint answers `200 {"enabled": false}` (a move id: `404`), the stream closes a client
at once and nothing is published.

## GET /api/diag

Open (readings only). The live document; the same document is streamed on `/ws/diag` and published
on `<base>/diag`.

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

- `log`: `ready`, or `unavailable` when its memory couldn't be allocated (then `move` is null).
- `simulation`: `off`, `bench` or `demo` (as in `/api/status`).
- `phase`: `idle`, `deadTime`, `slow`, `softStart`, `cruise`, `slowing`, `creep`.
- `direction`: `off`, `forward`, `reverse` (the driver command; which is "open" is a setting).
- `sagVolts`: rest − now while driven, else null. `powerWatts`: supply × current, null without
  either. `remainingMs`: null unless the roof moves with its position known.
- `driveBudgetUsedMs`: toward the move's target while moving; at rest the larger of the two ends.
- `watchdogMarginMs`: the watchdog limit minus the longest step gap since start.
- Demo: `control.stepUs`, `control.maxStepUs`, `loop`, `memory`, `wifi.rssi` and `resetReason` are
  null.
- `move`: the move in progress or the last one; null before the first.

## GET /api/diag/moves

Open. The summary list, newest first.

```json
{
  "enabled": true,
  "keep": false,
  "maxMoves": 5,
  "maxSummaries": 32,
  "moves": [ { "...": "a summary (below)" } ]
}
```

A summary:

```json
{
  "id": 12, "active": false, "kept": false, "profile": true,
  "startMs": 1203400, "endMs": 1253900, "startEpoch": 1762898400, "endEpoch": 1762898450,
  "origin": "person", "target": "open", "endState": "open", "result": "arrived",
  "fault": null,
  "durationMs": 50500, "drivenMs": 49900, "fromUnknown": false, "calibration": false,
  "peakAmps": 6.10, "averageAmps": 3.30, "stallAmps": null,
  "restVolts": 12.62, "minVolts": 12.01, "sagVolts": 0.61,
  "hallStart": 1020, "hallEnd": 3120
}
```

- `origin`: `person`, `automatic`, `client`, `mqtt` (`Roof::Origin`).
- `result`: `arrived`, `stopped`, `fault`; `fault` is `{"kind", "measured", "limit"}` for `fault`
  (`Roof::faultName`; units as in the status document), else null.
- `calibration`: the move was a leg of spec 016's calibration run.
- `kept`: loaded from the kept file: `startMs` and `endMs` are null, `profile` is false.
- `profile`: the move is still held with its profile (`GET /api/diag/moves/{id}`).
- Epochs are 0 when the clock wasn't set.

## GET /api/diag/moves/{id}

Open. `404 {"error": ...}` when the move isn't held.

```json
{
  "enabled": true,
  "move": { "...": "its summary" },
  "intervalMs": 500,
  "columns": ["tMs", "ampsAvg", "ampsMax", "voltsMin", "dutyPercent"],
  "points": [[0, 1.20, 4.80, 12.55, 40], [500, 2.95, 3.10, 12.20, 62]]
}
```

`?format=csv`: `text/csv`, `Content-Disposition: attachment; filename="roofroof-move-<id>.csv"`,
header `t_ms,amps_avg,amps_max,volts_min,duty_percent`, one row per point, empty fields for null.

## POST /api/diag/moves/clear

`requireAuth` and `X-RoofRoof: 1` (`403` without it). `409 {"error": ...}` while the roof moves or a
close waits. `200 {"success": true}`: the log and the list are empty; the kept file is removed by
the main loop through the flash-write guard at its next chance.

## WebSocket /ws/diag

The live document every 250 ms while the roof is opening or closing, every 2 s otherwise. At most 3
clients (the oldest is replaced); a client that stops reading is closed after 10 s. With the mode
off, a client is closed on connect.

## MQTT (with `diag.enabled`, `diag.mqtt` and MQTT on)

| Topic | Payload | When | Retained |
|---|---|---|---|
| `<base>/diag` | the live document | every 60 s at rest | no |
| `<base>/diag/move` | one summary | once per finished move, at rest | no |

At rest means: not moving, no close waiting, the automatic close not attempting. Nothing is
published on these topics otherwise.

## Firmware read interface (for other firmware code, e.g. spec 016)

```cpp
// Any task. False when the mode is off or the move isn't held.
bool RoofControl::diagMove(uint32_t id, Diag::Move &out) const;
// Any task. The move in progress or the last one (false when none).
bool RoofControl::diagLatest(Diag::Summary &out) const;
// Physical units of a point (NaN when none).
float Diag::pointAmpsAvg(const Diag::Point &), Diag::pointAmpsMax(...), Diag::pointVolts(...);
float Diag::pointDutyPercent(const Diag::Point &);
uint32_t Diag::pointStartMs(const Diag::Move &, uint8_t index);
```
