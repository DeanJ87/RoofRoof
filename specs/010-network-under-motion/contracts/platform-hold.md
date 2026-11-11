# Contract: platform hold rules

`RoofIo::platformHold(action, state)` returns the first reason that applies, in the order shown.

| Action | Roof opening or closing | A close waits | Close sequence not idle |
|---|---|---|---|
| `FlashWrite` | `Moving` | `CloseWaiting` | - |
| `Restart` | `Moving` | `CloseWaiting` | `CloseSequence` |
| `MandatoryRestart` | `Moving` | `CloseWaiting` | - |
| `FirmwareUpload` | `Moving` | `CloseWaiting` | `CloseSequence` |
| `WifiChange` | `Moving` | `CloseWaiting` | `CloseSequence` |
| `WifiScan` | `Moving` | `CloseWaiting` | - |

Texts (`RoofIo::holdText`):

| Reason | Text |
|---|---|
| `Moving` | The roof is moving: stop it first, then try again |
| `CloseWaiting` | A close is waiting to start: try again once the roof has closed |
| `CloseSequence` | The automatic close is under way: try again once the roof has closed or the weather is safe |

`RoofIo::hotspotRefuses(command, viaHotspot)`: true only for `Open` over the hotspot. Reason name
`hotspot`, text "Opening isn't available over the setup hotspot: use the home network".

## Command blockers (spec 003 FR-029, extended)

| Blocker | Open | Close | Stop / abort |
|---|---|---|---|
| upload or command-line OTA writing | refused `busy` | deferred, upload asked to stop | proceed |
| short flash write | refused `busy` | deferred | proceed |
| MQTT connect | refused `busy` | deferred | proceed |
| restart scheduled, cancellable | refused `busy` | cancels it, proceeds | proceed |
| restart scheduled, mandatory | refused `busy` | proceeds (the restart waits) | proceed |
| restart committed | refused `busy` | refused `busy` | proceed |

## HTTP

| Route | New 409 when |
|---|---|
| `PUT/POST /api/config`, `POST /api/backup/apply` | a close waits (moving was already refused); the apply also when its restart is held |
| `POST /api/restart` | a close waits or the close sequence is busy |
| `POST /api/update` | a close waits or the close sequence is busy |
| `POST /api/update/fs` | a close waits |
| `POST /api/wifi/connect` | a close waits or the close sequence is busy |
| `GET /api/wifi/scan` | the roof moves or a close waits |
| `POST /api/roof/open` | over the setup hotspot (`reason` `hotspot`) |

Every 409 carries `error` with the text above.
