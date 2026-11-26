# Implementation Plan: MQTT Telemetry (spec 046)

## Technical context

ESP32 Arduino, PubSubClient 2.8.0 with its 3 KB buffer allocated once; the main loop's roof state
check (`WebServer::publishRoofState`, every 100 ms while MQTT is connected) already takes a roof
snapshot and owns spec 032's pass budget (`MQTTClient::mayWrite`).

## Design

- **lib/DiagLogic `Telemetry.h/.cpp`** (pure, native-tested, in the demo core):
  `Telemetry::Schedule` (moving 1 Hz + 3 s tail; rest interval; 0.2 V rule with a 10 s floor;
  off), `Telemetry::MoveCue` (one record per finished-count step, re-primed on a new MQTT
  connection), `stampOf` (epoch ms or uptime ms), and `writeSample`, `writeRest`, `writeMove`
  writing into a caller's buffer with `vsnprintf` (0 when it doesn't fit; machine names checked to
  be plain identifiers).
- **Diag::Summary** gains `blind` (set from `Roof::Status::blind` during the move; not kept in the
  kept-summaries file).
- **RoofIo::Snapshot** gains `dutyPercent` and `origin` (filled in `RoofControl::refreshSnapshot`).
- **MQTTClient::publishSubtopicBytes**: topic built in a 160-byte stack buffer, payload from the
  caller's buffer, PubSubClient copies into its own preallocated buffer.
- **src/WebServerTelemetry.cpp**: `publishTelemetry(snapshot)` called at the end of
  `publishRoofState` (after the state document), `publishMoveRecord` checks the move log's finished
  count once a second (`RoofControl::diagFinished`, `diagLatest`).
- **Config**: `MQTTConfig::Telemetry {enabled, restVoltsSeconds}`, JSON in/out, validation (0-3600);
  the settings parse pool grows by 512 bytes for the new object.
- **Web**: a Telemetry group in Settings → Network → MQTT (one toggle, one number), zod schema,
  types, mock, i18n.

## Constitution check

No motion, gate or safety decision reads telemetry (Principles I-III untouched); it is output only.
Heap and timing rules (spec 031/035/038, spec 008/032 budgets) are followed as FR-005/FR-006 state.

## Heap and stack

No heap per publish. Stack on the main loop: at most 512 + 160 bytes plus `Diag::Summary` (~100 B)
in `publishMoveRecord`, 256 + 160 in `publishTelemetry`, not nested in each other's buffers.
