# Research: Data Interfaces

## R1. The origin of an MQTT command

- **Decision**: a new `Roof::Origin::Mqtt`. `RoofIo::checkCommand` applies the client rules to every
  remote origin (`remoteOrigin()`: `Client` and `Mqtt`); only the wording of a refusal names the
  source. Spec 002's controller already refuses anything but `Person` a move from unknown and a fault
  clear, and only `Person` stops count for the close sequence's stand-down.
- **Rationale**: MQTT has no login of its own (the broker is the only gate) and Home Assistant
  automations run unattended, so it must be no less restricted than an Alpaca client. A separate
  origin keeps the log and refusals honest ("MQTT", not "an imaging app") and lets a later spec
  tighten one source without the other.
- **Alternatives**: reuse `Origin::Client` (refusals would blame an imaging app); `Origin::Person`
  (would let MQTT move from unknown, clear faults and stand the automatic close down: violates
  constitution I and III).

## R2. Retained commands

- **Decision**: never act on a retained delivery. PubSubClient 2.8 doesn't pass the PUBLISH header
  to the callback, but the topic and payload pointers it passes sit at fixed offsets in its receive
  buffer: the header byte is at `topic - (L + 2)`, where `L` is the length of the packet's
  remaining-length field. `RoofIo::Mqtt::deliveryOf` works out the QoS from the gap between topic
  and payload (topic length + 1 for QoS 0, + 3 for QoS 1), recomputes the remaining length, and
  checks the packet type, the QoS bits, the remaining-length bytes and the topic length's high byte
  before trusting the retain bit. A message published with the retain flag still reaches current
  subscribers live (the broker clears the flag for them), so it runs once if the device is
  connected; only its replays are refused. Any mismatch is `Unknown`, which the command gate treats as
  retained. PubSubClient is pinned to 2.8.0 in `platformio.ini`.
- **Rationale**: an MQTT 3.1.1 broker sets RETAIN on a delivery only when it is sent because of a new
  subscription (§3.3.1.3), i.e. a message left on the broker. Acting on it would replay an old OPEN
  on every reconnect, opening the roof with nobody asking (constitution III). The checks make a
  library change fail safe instead of reading the wrong byte as the flag.
- **Alternatives**: ignore commands for a while after subscribing (drops real commands, still racy);
  clear the topic by publishing an empty retained message (can't tell what was retained); MQTT 5
  retain handling (PubSubClient is 3.1.1 only); a fork of the library (more to maintain than one
  checked function).

## R3. One document or one topic per value

- **Decision**: one retained JSON document on `<base>/state`: `timestamp`, `timeValid` and `roof`,
  where `roof` is written by the same `RoofIo::writeStatus` as `/api/status` (SQMeter spec 013
  FR-001: one schema across REST, WebSocket and MQTT).
- **Rationale**: one serializer can't drift; Home Assistant and Node-RED read JSON with templates;
  SQMeter does the same, so automations look alike.
- **Alternatives**: one plain topic per value (state, position, ...): easier for the simplest
  clients but a second serializer and several retained topics to keep consistent.

## R4. Publish cadence and the main loop

- **Decision**: publish at once after a connect. At rest: on a change of the state key (state,
  fault kind, position known, waiting close, close phase, weather verdict, park confirmed), at most
  once a second, otherwise every `mqtt.publishIntervalMs`. While the roof moves, a close waits or the
  automatic close is attempting (waiting for the park, closing, waiting to retry):
  only on a change of state, fault or waiting close, at most every 5 s, at most twice until it is
  at rest, and nothing once a publish on the connection has failed; discovery is held until it
  stops. The supply-low flag isn't in the key: it is one unfiltered reading and flaps under load.
- **Rationale**: PubSubClient writes on the main loop, and Arduino-ESP32's `NetworkClient::write`
  can wait up to ten 1 s selects for a socket whose send buffer is full (a half-open link). The
  roof's gates count the weather unsafe and the park lost when the main loop hasn't handed them
  over for 5 s, which stops a close as a motion fault. Periodic ~1 KB documents during a move would
  fill a dead link's 5.7 KB send buffer, and documents queued at rest (during the park wait) can
  fill it before the close starts. So, besides the limits above, every write on the MQTT socket
  (publishes, PINGREQ, alerts) gets a budget (`DualStackClient::setWriteBudget`), 250 ms while the
  roof is busy (the reconnect hold applies) and 1 s at rest, so the burst after a connect gets
  through a slow link: a write
  that can't finish in time closes the connection, and later writes fail at once. The reconnect
  is held while the roof is busy (spec 010). A main-loop pass can then lose at most about 250 ms to
  MQTT writes, far inside the 5 s hand-over limit. Reads were the other wait: PubSubClient waits up
  to its 3 s socket timeout for the rest of a message. So while busy the whole pass shares 500 ms
  (`RoofIo::Mqtt::PassBudget`, started at the top of the main loop), the socket reads into its own
  buffer without waiting, and PubSubClient's loop runs only when nothing or a whole message is
  buffered (`packetComplete`), with its reads held to that buffer. The live position is in
  `/ws/status` (async task). Spec 010 owns the network under motion as a whole.
- **Alternatives**: every 2 s while moving (SQMeter's interval idea; rejected for the risk above);
  a separate MQTT task (a second owner of PubSubClient, which isn't thread-safe).

## R5. Home Assistant entities

- **Decision**: under the existing device:
  - `cover/roof` "Roof": `state_topic` `<base>/state`, a template mapping `open`, `opening`,
    `closed`, `closing`, `stopped` through and anything else (`error`, `unknown`) to `None` (Home
    Assistant's unknown). `command_topic` `<base>/roof/set` only while roof commands are on; Home
    Assistant's default payloads (`OPEN`, `CLOSE`, `STOP`) and state names match the device, so they
    aren't repeated. Without a command topic the cover is read-only.
  - sensors: `position` (%), `supply` (V, voltage), `motor_current` (A, current; only with current
    sense on), `hall` (counts, diagnostic); `state_class: measurement`.
  - `binary_sensor/fault` "Roof fault", `device_class: problem`, `ON` while `roof.fault` isn't null,
    attributes from the fault object.
  - All available only while `<base>/availability` is `online`.
- **Rationale**: the roadmap's entity list; null values render as `None`, which Home Assistant's MQTT
  sensor shows as unknown (SQMeter does the same). No cover position: the roof has no set-position,
  and a cover with a position invites one.

## R6. Where the weather and park in the documents come from

- **Decision**: `RoofControl::snapshot()` adds the gates as `RoofIo::currentGates` sees them at that
  moment: the verdict and its text, and `parkHolds()` with the park result's text or the gate's
  refusal text. The close phase comes from `CloseOnUnsafe::phase()` (an atomic, set every step on
  the main loop) through a callback, as the alert conditions do.
- **Rationale**: it is what decides whether a command runs, including a stale hand-over, so the
  dashboard and Home Assistant never show "safe" while the roof would refuse to open.

## R7. Command results

- **Decision**: `<base>/roof/result`, not retained, one per command: `command` (null when the
  payload wasn't one), `accepted`, `reason` (machine name), `text`, `state`. Published from the web
  server's loop pass, not inside PubSubClient's callback.
- **Rationale**: constitution III wants every refusal's reason stated; publishing from inside the
  callback would reuse PubSubClient's buffer while it still holds the incoming packet.

## R8. The setting

- **Decision**: `mqtt.roofCommands`, boolean, default false, no dependency beyond MQTT itself
  (settings-deps `noDependency`). In the MQTT card under the Home Assistant switch, with a "?" tip.
- **Rationale**: opening the roof from a network message is opt-in; an older config loads with it
  off (constitution: a migration never turns on a motion feature).
