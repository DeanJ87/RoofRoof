# Data Model: Alpaca Dome

## Alpaca side (`lib/AlpacaLogic/include/AlpacaDome.h`, no roof types)

| Type | Fields / values | Notes |
|---|---|---|
| `ShutterState` | `Open` 0, `Closed` 1, `Opening` 2, `Closing` 3, `Error` 4 | ASCOM `ShutterState` numbers |
| `DomeStatus` | `shutter` (`ShutterState`, default `Error`), `slewing` (bool) | read from the backend for every status member |
| `DomeAction` | `OpenShutter`, `CloseShutter`, `AbortSlew` | the only Dome members that act |
| `DomeAnswer` | `accepted` (bool), `message` (text when refused) | refused → InvalidOperation with `message` |
| `domeDeviceInfo()` | type `Dome`, key `dome`, number 0, name `RoofRoof roof`, description `Roll-off roof: shutter open, close and stop only`, interface version 3 | the one device the firmware and simulator list |

`Backend` gains:

- `DomeStatus domeStatus() const`
- `DomeAnswer domeCommand(DomeAction action)`: called only for a request the router has already
  checked (Alpaca on; connected for open and close).

## Roof side (`lib/RoofIoLogic/include/DomeBridge.h`)

| Function | In | Out |
|---|---|---|
| `domeStatus(status, closePending, running)` | `Roof::Status`, a close waiting, controller running | `Alpaca::DomeStatus` (table below) |
| `roofCommand(action)` | `DomeAction` | `Open`, `Close`, `Stop` (abort keeps the estimate) |
| `countsAsPersonStop(command, origin)` | `Roof::Command`, `Roof::Origin` | true for `Stop`/`Abort` from `Person` only |
| `domeAnswer(command, accepted, reason, text, fault)` | an outcome | `DomeAnswer`; a stop is always accepted |

`lib/RoofIoLogic/include/CommandCheck.h`: `checkCommand(command, origin, gates, controller, closePending)`
→ `{accepted, reason, text}`: stops always pass; open and close pass the weather and park gates; a
client's open is refused while closing or a close waits (`closing`); then the controller's dry run.
`RoofControl::precheck` and the simulator both call it.
| `domeRefusalText(reason, text, fault)` | refusal machine name and text, the latched fault | the text, plus `: ` and the fault's description when the reason is `faultLatched` |

### Shutter state table

| Running | Roof state | Close waiting | Shutter | Slewing |
|---|---|---|---|---|
| no | any | any | Error | false |
| yes | Closed | no | Closed | false |
| yes | Closed | yes | Closed | false |
| yes | Open | no | Open | false |
| yes | Open | yes | Closing | true |
| yes | Stopped | no | Error | false |
| yes | Stopped | yes | Closing | true |
| yes | Opening | any | Opening | true |
| yes | Closing | any | Closing | true |
| yes | Unknown | no | Error | false |
| yes | Unknown | yes | Closing | true |
| yes | Error | any | Error | false |

A close waiting from Closed can't happen (a close to a closed roof runs at once and does nothing);
the row is there so the function is total.

## Origin (`lib/RoofLogic/include/RoofTypes.h`)

`Person` (authenticated web UI), `Automatic` (spec 005), `Client` (Alpaca). Only `Person` may move
from an unknown position or clear a fault.
