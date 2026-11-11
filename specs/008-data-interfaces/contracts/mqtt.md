# Contract: MQTT topics and Home Assistant entities

`<base>` is `mqtt.topic` (default `roofroof`). Existing topics (availability, alerts,
alerts/armed, alerts/armed/set) are unchanged.

## Topics added

| Topic | Direction | Retained | Payload | When |
|---|---|---|---|---|
| `<base>/state` | out | yes | the roof document | on connect; at rest, on a change of state, fault, position known, close phase, weather or park (at most once a second) and every publish interval; while moving, a close waits or the automatic close is attempting, at most twice, 5 s apart, on a change of state, fault, waiting close or attempt |
| `<base>/roof/set` | in | must not be | `OPEN`, `CLOSE`, `STOP` (any case, whitespace trimmed) | command |
| `<base>/roof/result` | out | no | `{command, accepted, reason, text, state}` | one per command received |

## The roof document

```json
{
  "timestamp": 1794480000,
  "timeValid": true,
  "roof": {
    "state": "closing",
    "positionKnown": true,
    "positionMm": 925,
    "positionPercent": 50,
    "target": "closed",
    "driving": true,
    "fromUnknown": false,
    "moveElapsedMs": 24000,
    "moveTimeoutMs": 66000,
    "hall": { "level": "between", "counts": 2010 },
    "supplyVolts": 12.31,
    "supplyLow": false,
    "minSupplyVolts": 11,
    "currentSense": false,
    "motorAmps": null,
    "maxCurrentAmps": 10,
    "parkSwitches": { "fitted": 2, "switch1": "closed", "switch2": "closed" },
    "rainContact": "notFitted",
    "closePending": false,
    "closePhase": "closing",
    "weather": "unsafe",
    "weatherText": "unsafe: rain (SQMeter HTTP)",
    "parkConfirmed": true,
    "parkText": "parked (N.I.N.A. AtPark, 2 switches closed)",
    "fault": null,
    "watchdogTrips": 0,
    "maxStepGapMs": 21,
    "maxWatchdogBeatAgeMs": 20
  }
}
```

`roof` is the `roof` object of `GET /api/status` and `/ws/status`, written by the same code.
`timestamp` is 0 and `timeValid` false until the clock is set. Unreadable values are null.

## Command result

```json
{ "command": "open", "accepted": false, "reason": "weatherNotSafe",
  "text": "Weather not safe: unsafe: rain (SQMeter HTTP)", "state": "closed" }
```

Reasons: `switchedOff`, `retained`, `notACommand`, `notStarted`, the gates' `weatherNotSafe` and
`mountNotParked`, the remote rules' `unsafeClose` and `closing`, `busy`, `restarting`, `hotspot`, and the motion logic's
refusals (`positionUnknown`, `faultLatched`, ...). `command` is null for `notACommand`.

## Home Assistant

Config topics `<prefix>/<component>/roofroof_<mac>/<object>/config`, retained; every entity is
available while `<base>/availability` is `online` and belongs to the device `roofroof_<mac>`.

| Component | Object | Name | Reads (`<base>/state`) | Unit / class | Announced |
|---|---|---|---|---|---|
| `binary_sensor` | `connectivity` | Connectivity | `<base>/availability` | connectivity | always (existing) |
| `switch` | `alerts` | Alerts | `alerts/armed` | | alerts on (existing) |
| `cover` | `roof` | Roof | `roof.state` (`error`/`unknown` → unknown) | | always; `command_topic` `<base>/roof/set` only with `mqtt.roofCommands` |
| `sensor` | `position` | Roof position | `roof.positionPercent` | % | always |
| `sensor` | `supply` | Supply voltage | `roof.supplyVolts` | V, voltage | always |
| `sensor` | `motor_current` | Motor current | `roof.motorAmps` | A, current | current sense on |
| `sensor` | `hall` | Hall reading | `roof.hall.counts` | diagnostic | always |
| `binary_sensor` | `fault` | Roof fault | `roof.fault` not null; attributes = the fault | problem | always |

An entity that isn't announced gets an empty retained config (removed).
