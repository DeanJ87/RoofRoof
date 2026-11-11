# Data Model: Data Interfaces

## Roof::Origin (lib/RoofLogic, extended)

| Value | Who | Person? | Remote? |
|---|---|---|---|
| `Person` | the authenticated web UI | yes | no |
| `Automatic` | the controller (spec 005's close, the gates' stops) | no | no |
| `Client` | an ASCOM Alpaca client (spec 006) | no | yes |
| `Mqtt` | a command on `<base>/roof/set` (this spec) | no | yes |

`RoofIo::remoteOrigin(origin)` is true for `Client` and `Mqtt`. Remote origins get the client rules
in `RoofIo::checkCommand`: no stop while a close runs or waits and the weather isn't safe (reason
`unsafeClose`), no open while a close runs, waits or was stopped part-way (reason `closing`).
`countsAsPersonStop` stays `Person` only.

## RoofIo::Snapshot (lib/RoofIoLogic, extended)

| Field | Type | Source |
|---|---|---|
| `weather` | `Safety::Verdict` | `currentGates().weather` at snapshot time |
| `weatherText` | string | `currentGates().weatherText` |
| `parkConfirmed` | bool | `parkHolds(currentGates())` |
| `parkText` | string | the park result's text when confirmed, else `describeGate(MountNotParked)` |
| `closePhase` | `const char *` | `CloseSeq::phaseName(CloseOnUnsafe::phase())`, set by the web server; `"idle"` by default |

## Roof section (status, WebSocket, MQTT), added keys

| Key | Type | Values |
|---|---|---|
| `closePhase` | string | `idle`, `waitPark`, `closing`, `retryWait`, `heldByPerson`, `cannotMove`, `halted` |
| `weather` | string | `safe`, `unsafe`, `unknown` |
| `weatherText` | string | the verdict's reason, e.g. `unsafe: rain (SQMeter HTTP)` |
| `parkConfirmed` | bool | the close gate would pass now |
| `parkText` | string | why, e.g. `Mount not parked: Park switch 1 open` |

## RoofIo::Mqtt (lib/RoofIoLogic/RoofMqtt, new)

- `Delivery { Live, Retained, Unknown }` and `deliveryOf(topic, topicLength, payload, length)`
  (PubSubClient 2.8.0 buffer layout; research R2).
- `Request { run, command, reason, text }` and `parseCommand(payload, live, enabled)`:

  | Case | run | reason | text |
  |---|---|---|---|
  | setting off | no | `switchedOff` | Roof commands over MQTT are switched off (Settings > Network > MQTT) |
  | retained or unknown flag | no | `retained` | A retained roof command is never acted on: publish it without the retain flag |
  | not OPEN/CLOSE/STOP | no | `notACommand` | Not a roof command: send OPEN, CLOSE or STOP |
  | OPEN / CLOSE / STOP | yes | | |

  The order is: switched off, then retained, then the payload.
- `writeResult(out, command, accepted, reason, text, state)`: `{command, accepted, reason, text, state}`
  (`command` null when not a command; `reason` and `text` empty strings when accepted).
- `writeState(out, snapshot, epoch, clockValid)`: `{timestamp, timeValid, roof: writeStatus(...)}`.
- `StateKey` (state, fault kind, position known, close pending, close phase, weather, park
  confirmed, supply low) and `keyOf(snapshot)`.
- `StateSchedule::due(key, moving, connection, nowMs, intervalMs)` and `published(key, connection, nowMs)`:
  due when never published or a new connection; when the key changed and 250 ms have passed since
  the last publish; when not moving and `intervalMs` has passed.

## MQTTConfig (lib/ConfigModel, extended)

| Field | JSON | Default | Notes |
|---|---|---|---|
| `roofCommands` | `mqtt.roofCommands` | `false` | missing in an older config → false |

## HaDiscovery (lib/HaDiscovery, extended)

`Groups` gains `roofCommands` (the cover gets its command topic) and `currentSense` (the motor
current sensor is announced). Entities: see [contracts/mqtt.md](contracts/mqtt.md#home-assistant).
