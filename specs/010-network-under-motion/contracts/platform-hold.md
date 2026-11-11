# Contract: platform hold rules

`RoofIo::platformHold(action, state)` returns the first reason that applies, in the order shown.

The close sequence's phases map to `RoofIo::SequenceState` (`CloseSeq::platformState`): WaitPark
and Closing are `Working`; HeldByPerson is `HeldByPerson`; RetryWait, CannotMove and Halted are
`Waiting`; Idle is `Idle`.

| Action | Roof opening or closing | A close waits | Sequence `Working`, network up | `Working`, network down |
|---|---|---|---|---|
| `FlashWrite` | `Moving` | `CloseWaiting` | - | - |
| `FilesystemUpload` | `Moving` | `CloseWaiting` | `CloseSequence` | `CloseSequence` |
| `Restart` | `Moving` | `CloseWaiting` | `CloseSequence` | - |
| `MandatoryRestart` | `Moving` | `CloseWaiting` | - | - |
| `FirmwareUpload` | `Moving` | `CloseWaiting` | `CloseSequence` | `CloseSequence` |
| `WifiChange` | `Moving` | `CloseWaiting` | `CloseSequence` | - |
| `WifiScan` | `Moving` | `CloseWaiting` | `CloseSequence` | - |

"Network up" is the home network joined with the setup hotspot off. `Waiting`, `HeldByPerson` and
`Idle` hold nothing: the sequence can wait there for hours, often because the network is
broken, and a WiFi fix, a restart or an update must stay possible.

Texts (`RoofIo::holdText`):

| Reason | Text |
|---|---|
| `Moving` | The roof is moving: stop it first, then try again |
| `CloseWaiting` | A close is waiting to start: try again once the roof has closed |
| `CloseSequence` | The automatic close is under way: try again once the roof has closed or the weather is safe |

`Blockers::hotspotOn` (decided under the roof's lock): `Open` is refused while the hotspot is on.
Reason name `hotspot`, text "Opening isn't available while the setup hotspot is on".

The restart timer also holds an ordinary restart while an upload or short write runs.

## Command blockers (spec 003 FR-029, extended)

| Blocker | Open | Close | Stop / abort |
|---|---|---|---|
| upload or command-line OTA writing | refused `busy` | deferred, upload asked to stop | proceed |
| short flash write | refused `busy` | deferred | proceed |
| WiFi change (until joined or timed out) or scan | refused `busy` | deferred | proceed |
| MQTT connect | refused `busy` | deferred | proceed |
| restart scheduled, cancellable | refused `busy` | cancels it, proceeds | proceed |
| restart scheduled, mandatory | refused `busy` | proceeds (the restart waits) | proceed |
| restart committed | refused `restarting` | refused `restarting` (the close sequence answers it as "not run") | proceed |

## HTTP

| Route | New 409 when |
|---|---|
| `PUT/POST /api/config`, `POST /api/backup/apply` | a close waits (moving was already refused); the apply also when its restart is held |
| `POST /api/restart` | a close waits or the close sequence is working |
| `POST /api/update` | a close waits or the close sequence is working |
| `POST /api/update/fs` | a close waits or the close sequence is working |
| `POST /api/wifi/connect` | a close waits or the close sequence is working |
| `GET /api/wifi/scan` | the roof moves, a close waits or the close sequence is working |
| `POST /api/roof/open` | while the setup hotspot is on (`reason` `hotspot`) |

Every 409 carries `error` with the text above.
