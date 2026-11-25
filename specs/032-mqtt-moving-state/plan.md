# Implementation Plan: The Roof's MQTT State While It Moves

**Branch**: `spec/032-mqtt-moving-state` | **Spec**: [spec.md](spec.md)

## Summary

Change `RoofIo::Mqtt::StateSchedule` (lib, pure) so a move publishes at its start, every 3 s and at
its end; make a failed publish back off instead of latching; add `PassBudget::mayWrite` so a busy
state publish only starts with a whole write budget left. `WebServer::publishRoofState` asks
`MQTTClient::mayWrite()` before building the document. A native test pins the largest moving
document inside the client buffer (`CLIENT_BUFFER_BYTES`, now used by `MQTTClient`).

## Constitution check

- I/II/III (safety, interlocks, weather): no command or interlock path changes; MQTT stays
  publish-only here.
- VIII (main loop): every write while busy keeps spec 008's budgets; more publishes per move, each
  bounded, and `mayWrite` removes the cut-short case.
- VI (flash): a few hundred bytes. No web or LittleFS change.
- IX (docs): `docs/user-guide/mqtt.md`, `docs/integration/home-assistant.md`.

## Files

- `lib/RoofIoLogic/include/RoofMqtt.h`, `lib/RoofIoLogic/src/RoofMqtt.cpp`: schedule, `moving()`,
  `MOVING_PROGRESS_MS`, `PassBudget::mayWrite`, `CLIENT_BUFFER_BYTES`.
- `include/MQTTClient.h`, `src/MQTTClient.cpp`, `src/WebServerRoof.cpp`.
- `test/test_mqtt_schedule`, `test/test_roof_mqtt`.
- Demo core rebuilt (lib change).
