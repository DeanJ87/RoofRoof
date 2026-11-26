# Tasks: MQTT Telemetry (spec 046)

- [x] T001 `lib/DiagLogic/include/Telemetry.h`, `src/Telemetry.cpp`: schedule, move cue, stamp, writers
- [x] T002 `Diag::Summary::blind`, set by `MoveLog`
- [x] T003 `RoofIo::Snapshot::dutyPercent`, `origin`; filled in `RoofControl::refreshSnapshot`
- [x] T004 `MQTTClient::publishSubtopicBytes` (stack topic, no heap)
- [x] T005 `src/WebServerTelemetry.cpp`, called from `publishRoofState` after the state document
- [x] T006 `mqtt.telemetry` settings: model, JSON in/out, validation, ranges fixture, settings-deps catalogue, default fixture, config schema, OpenAPI
- [x] T007 Web: Settings → Network → MQTT → Telemetry, schema, types, mock, i18n, tests
- [x] T008 `test/test_telemetry`: cadence moving vs rest, 0.2 V rule, off, record fields, one per move, buffer sizes
- [x] T009 Docs: `docs/user-guide/mqtt.md` (payloads, Telegraf json_v2 example), configuration, Home Assistant topic list, diagnostics, DIA-01 and DIA-09; CHANGELOG
- [x] T010 Demo core rebuilt; firmware build, heap_budget, stack_depth, quality, LittleFS size
- [ ] T011 Owner: after a flash, watch `mosquitto_sub -t 'roofroof/telemetry' -t 'roofroof/move' -v` through one open and close
