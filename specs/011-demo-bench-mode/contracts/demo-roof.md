# Contract: The demo's roof

## Device core bindings (tools/demo-core/bridge.cpp)

| Binding | Returns |
|---|---|
| `roofCommand(name)` (`open`, `close`, `stop`, `clear-fault`) | `{status, body}`: the device's `/api/roof/*` reply (200, 202 pending, 409 refused) |
| `roofInject(json)` | the panel view after the change |
| `roofPanel()` | `{injectors…, state, weather, park, phase}` |

`statusParts()` gains `roof` (the firmware's `writeStatus`, `simulation: "demo"`). `saveState()`
and `loadState()` carry the roof's saved state. `alpaca()` lists and answers the Dome.

## REST in the demo

`POST /api/roof/open|close|stop|clear-fault`: the core's reply, as the device's (spec 003
contracts/roof-hardware.md). The header and login checks of the device aren't modelled.

## Demo panel: Roof

| Control | Values | Effect |
|---|---|---|
| Weather | Safe / Unsafe / Rain | the simulated SQMeter's answer |
| Mount | Parked / Unparked | the imaging app parks (8 s slew) or unparks the mount |
| Park fails | off / on | N.I.N.A. never reaches park |
| Jam | off / on | the roof can't move; the motor stalls |
| 12 V supply | Normal / Low | 12.6 V / 10.5 V |
