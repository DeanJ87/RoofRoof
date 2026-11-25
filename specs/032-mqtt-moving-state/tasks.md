# Tasks: The Roof's MQTT State While It Moves

- [x] T001 Native tests first, failing on the old schedule: a move's start, progress and end; a whole 57 s close at 100 ms checks; a failure never silencing later documents; `mayWrite` (`test/test_mqtt_schedule`)
- [x] T002 The largest moving document fits the client buffer with a long topic (`test/test_roof_mqtt`)
- [x] T003 `StateSchedule::due`/`published`, `moving()`, `MOVING_PROGRESS_MS`, `PassBudget::mayWrite`, `CLIENT_BUFFER_BYTES` (`lib/RoofIoLogic`)
- [x] T004 `MQTTClient::mayWrite` and its use in `WebServer::publishRoofState`; the buffer size from the lib constant
- [x] T005 Docs: `docs/user-guide/mqtt.md`, `docs/integration/home-assistant.md`
- [x] T006 Demo core rebuilt; quality gate, native tests, firmware build, flash budget, docs build
