# Contract: the calibration routes

All three are registered before any shorter `/api/roof/...` route they could prefix-match.

## `POST /api/roof/calibration/start`

`requireAuth`, header `X-RoofRoof: 1` (403 without it).

```json
{ "attended": true, "at": "closed", "travelMm": 1850, "topPass": false }
```

- `attended` must be `true`; `at` is `closed`, `open` or `partWay`; `travelMm` 100-20000;
  `topPass` optional (default false).
- 200 `{"success": true, "state": "running"}`.
- 400 `{"error": "..."}` for a body that isn't this shape.
- 409 `{"success": false, "reason": "<name>", "error": "<text>"}` for a refusal. Reasons:
  `notAttended`, `calibrating`, `closeSequence`, `faultLatched`, `moving`, `closeWaiting`,
  `settingsPending`, `weatherNotSafe`, `daylight`, `mountNotParked`, `busy`, `hotspot`,
  `restarting`, `invalid`, `hallDisagrees`, `notStarted`, and the motion logic's refusals
  (`supplyLow`, `supplyUnreadable`, `currentUnreadable`, `hallNotSettled`, `settingsInvalid`).

## `POST /api/roof/calibration/confirm`

`requireAuth`, `X-RoofRoof: 1`. Body `{"atOpenEnd": true|false}` (400 otherwise). Only while
`leg.phase` is `confirm` (409 `notWaiting` otherwise): `true` continues with the slow close,
`false` ends the run (`wrongEnd`). Answers like `POST /api/roof/stop`.

## `POST /api/roof/calibration/abort`

`requireAuth`, `X-RoofRoof: 1`. Stops the roof as a person's stop and ends any run. Answers like
`POST /api/roof/stop`: 200 `{"success": true, "state": "<roof state>"}`.

## `GET /api/roof/calibration`

Open (readings only).

```json
{
  "state": "done",
  "active": false,
  "request": { "at": "closed", "travelMm": 1850, "topPass": false, "limitSeconds": 180 },
  "leg": null,
  "ending": { "reason": "done", "text": "Calibration finished" },
  "legs": [
    { "name": "slowOpen", "target": "open", "dutyPercent": 40, "drivenMs": 121650,
      "level": "high", "speedMmPerSecond": 38.0, "ampsMean": null, "ampsPeak": null,
      "supplyMinVolts": 12.5 }
  ],
  "hall": {
    "between": { "mean": 2050.0, "min": 2050, "max": 2050, "noise": 0.0 },
    "closed": { "mean": 700.0, "min": 700, "max": 700, "level": "low" },
    "open": { "mean": 3400.0, "min": 3400, "max": 3400, "level": "high" }
  },
  "currentSense": false,
  "maxCurrentAmps": 10.0,
  "proposal": {
    "travelMm": { "old": 1850, "new": 1850 },
    "speedMmPerSecond": { "old": 38.0, "new": 38.0 },
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

- `state`: `idle` (no run since start-up), `running`, `done`, `failed`, `stopped`.
- `leg` while running: `{ "index", "count", "name", "phase" ("settle", "wait", "confirm", "drive"),
  "elapsedMs", "limitMs" }`.
- `ending`: `{ "reason", "text" }` once ended; reasons `done`, `stopped`, `weather`, `park`,
  `closeSequence`, `fault`, `refused`, `busy`, `wrongEnd`, `unconfirmed`.
- `legs[].name`: `slowOpen`, `slowClose`, `topOpen`, `topClose` (the first close isn't measured).
  `ampsMean`, `ampsPeak` are null without current sense.
- `hall.*` null until read.
- `proposal`: null unless `state` is `done`. A withheld value is null, with its `speedProblem`
  or `hallProblem` (`{ "reason", "text" }`; reasons `legsDisagree`, `legTooShort`,
  `outOfRange`; `samePole`, `weakEnd`, `noRoom`). `warnings[]` `{ "reason", "text" }`:
  `directionsDiffer`, `currentNearLimit`.

## The status document

`roof.calibrating` (boolean) in `GET /api/status` and `/ws/status`.
