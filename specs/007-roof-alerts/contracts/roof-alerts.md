# Contract: Roof Alerts

## Alert payloads (webhook body, MQTT `<topic>/alerts`)

Unchanged shape (`Delivery::alertJson`); the `event` field carries the new names:
`mount_not_parked`, `roof_not_closed`, `move_fault`, `supply_low`, `unsafe_closing`, `roof_closed`,
`safety_lost`. A stacked notification lists every event, lead first, in `events`. The `roof` name is no
longer sent.

```json
{
  "event": "mount_not_parked",
  "title": "Mount didn't park",
  "message": "The weather is unsafe and the park wasn't confirmed within 240 s, so the roof stays open. Park: not parked: N.I.N.A. unreachable: connection refused",
  "level": "wake",
  "device": "RoofRoof",
  "timestamp": 1794470400
}
```

## Settings (`GET/POST /api/config`, `alerts.events`)

```json
"events": {
  "mount_not_parked": { "level": 4, "sound": "", "title": "", "message": "" },
  "roof_not_closed": { "level": 4, "sound": "", "title": "", "message": "" },
  "move_fault": { "level": 3, "sound": "", "title": "", "message": "" },
  "supply_low": { "level": 2, "sound": "", "title": "", "message": "" },
  "unsafe_closing": { "level": 2, "sound": "", "title": "", "message": "" },
  "roof_closed": { "level": 2, "sound": "", "title": "", "message": "" },
  "safety_lost": { "level": 2, "sound": "", "title": "", "message": "" },
  "client_lost": { "level": 3, "sound": "", "title": "", "message": "" },
  "client_back": { "level": 1, "sound": "", "title": "", "message": "" },
  "client_disconnected": { "level": 0, "sound": "", "title": "", "message": "" }
}
```

Refused (400, message): a level above 4 for any event; a level below 3 for `mount_not_parked` or
`roof_not_closed` ("Alerts: the roof's critical alerts are urgent or wake me").

## Test send (`POST /api/alerts/test?event=<name>&level=..&sound=..&title=..&message=..`)

`event` accepts the seven new names. The sample fills every variable with an example value; a
critical sample is sent at urgent or above.

## Firmware interfaces

- `Alerts::AlertType` (seven new values), `Alerts::isCritical(type)`, `Alerts::RoofInputs`,
  `AlertEngine::announcedRoof()` / `restoreAnnouncedRoof(bits)`.
- `CloseSeq::alertTypeFor(Event)`, `CloseSeq::criticalAlert(decision, why)`,
  `CloseSeq::roofConditions(...)`, `CloseSeq::attempting(phase)`.
- `Core::finishCriticalAlert(alert, sources, clock)`.
- `CloseOnUnsafe::conditions()`; `WebServer::setRoofConditions(inputs)`.
- `RoofIo::describeSupply(volts, minimum)`.
