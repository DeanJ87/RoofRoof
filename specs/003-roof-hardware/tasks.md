# Tasks: Roof Hardware

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/roof-hardware.md](contracts/roof-hardware.md),
[quickstart.md](quickstart.md)

**Tests**: every decision in `lib/RoofIoLogic` is written test first (constitution V). The firmware
glue is verified by the build and on the bench (the PR's bench-check list).

**Organisation**: the pure rules are foundational (Phase 2) because every story's glue calls them.
Each task ends with `pio test -e native -f "test_roof_*"` green; each phase ends with the firmware
build.

## Phase 1: Setup

- [x] T001 Create `lib/RoofIoLogic/{include,src}` and add `-Ilib/RoofIoLogic/include` to `[env:native]` in `platformio.ini`
- [x] T002 [P] Create the native suites `test/test_roof_io`, `test/test_roof_gates` and `test/test_roof_status`

## Phase 2: Foundational (pure rules, test first)

- [x] T003 [P] Tests then `lib/RoofIoLogic/include/RoofReadings.h`, `src/RoofReadings.cpp`: fixed values, `medianCounts` (odd, even, rails kept, a single spike rejected), `averageMillivolts`, `supplyVolts` (×122/22, NaN in → NaN), `motorAmps` (larger side, ×30/20 × A/V, NaN handling) in `test/test_roof_io/test_main.cpp`
- [x] T004 [P] Tests then `SwitchInput.h`/`.cpp`: unreadable until three equal readings, low = closed, a single flip doesn't change the level, chatter for more than 1000 ms is unreadable (at 1000 ms not yet), recovers after three equal readings, wrap-safe; `parkReading`, `rainReading`, `contactName`
- [x] T005 [P] Tests then `StepWatchdog.h`: `watchdogExpired` false at 100 ms and true at 101 ms, across the 32-bit wrap; `stepGapMs`
- [x] T006 [P] Tests then `FaultRecord.h`/`.cpp`: round trip of every `FaultKind` and `FaultCause`, length 0 = none, every damaged form (magic, version, kind None or out of range, cause out of range, checksum, length) = unreadable; `causeName`
- [x] T007 [P] Tests then `RoofGates.h`/`.cpp` in `test/test_roof_gates/test_main.cpp`: open needs `Safe` (Unsafe and Unknown refused); close needs parked AND every fitted switch closed (0, 1, 2 fitted; unreadable refused); stop and abort never gated; `interlockLost` only while closing; `interlockBits`; `openMustStop` only while opening; `gateRefusalName`
- [x] T008 [P] Tests then `SettingsGuard.h`/`.cpp`: each `roof` and `park` field counts as a change, other sections don't; refused only while opening or closing
- [x] T009 [P] Tests then `RoofText.h`/`.cpp` in `test/test_roof_status/test_main.cpp`: every fault kind and cause, every refusal, both gate refusals, with the measured value and the limit or the failed input (FR-022)
- [x] T010 Tests then `RoofStatus.h`/`.cpp`: the `roof` section for closed, opening, unknown (nulls), unreadable supply, current sense off (null), a fault with text, the switch and rain readings; size well under 1 KB
- [x] T011 Add `roof.currentAmpsPerVolt` (default 8.5, 0.5-100) to `lib/ConfigModel` (Config.h, defaults, JSON in/out, validation), `test/fixtures/config-ranges.json`, `lib/SettingsDeps/catalogue.json`, `web/src/types/config.ts`, `web/src/validation/roofSchema.ts`, `web/src/mocks/config.ts`, the config tests (model, migration) and the web schema tests

## Phase 3: User Story 1 - Move the roof from the API, safely (P1)

**Goal**: open, close and stop on a person's authenticated command, gated by weather and park.

- [x] T012 [US1] Extend `include/MotorDriver.h`, `src/MotorDriver.cpp`: `begin()` (LEDC 20 kHz, 8 bits, duty 0), `apply(Driver::Outputs)` (other PWM to 0, own PWM, EN last in a critical section; off: EN first), `trip()`, `tripped()`, `clearTrip()`, `enabled()`
- [x] T013 [US1] Write `include/RoofControl.h`, `src/RoofControl.cpp`: the controller under a mutex, `command` (gates, then the controller, then apply), `setSafety`, the switch readings for SafetyInputs, `settingsSaveRefused`, `updateConfig` (pending settings), `snapshot`, `revision`
- [x] T014 [US1] Write `src/RoofControlTask.cpp`: the 20 ms task (inputs outside the lock, then pending settings, watchdog trip, interlock, open stop, tick, apply, snapshot), the ADC and GPIO setup and reads
- [x] T015 [US1] Wire `src/main.cpp`: `RoofControl::begin` after the config loads; the loop passes park switches and the rain contact to `SafetyInputs` and the verdict and park result back to `RoofControl`; the save callback hands the config over
- [x] T016 [US1] Write `src/WebServerRoof.cpp`: `POST /api/roof/open|close|stop` with `requireAuth`, 200/409 bodies; register in `WebServer::setupAPIRoutes` before general routes; `include/WebServer.h` takes the `RoofControl`
- [x] T017 [US1] Refuse motion-settings saves while moving in `WebServer::handleSetConfig` and `handleBackupApply` (409 with the reason)

## Phase 4: User Story 2 - The motor never runs unsupervised (P1)

- [x] T018 [US2] The watchdog `esp_timer` (10 ms) in `src/RoofControlTask.cpp`: trips `MotorDriver` when the driver is on and `watchdogExpired`; the next step latches `External`/`watchdog` with the gap, then clears the trip; count trips and the longest step gap; subscribe the task to the task watchdog

## Phase 5: User Story 3 - A fault survives a restart (P1)

- [x] T019 [US3] Write `include/RoofFaultStore.h`, `src/RoofFaultStore.cpp`: load (none / fault / unreadable), save, remove, using `encodeFault`/`decodeFault`
- [x] T020 [US3] In `RoofControl`: latch the stored fault before the task starts; after a step that changed the fault, save or remove the record outside the lock; log failures

## Phase 6: User Story 4 - See the roof's readings and state (P2)

- [x] T021 [US4] `WebServer::appendRoof` (status document) from `RoofControl::snapshot()` and `RoofIo::writeStatus`; raise `Core::STATUS_DOC_CAPACITY` if the measured document needs it; broadcast `/ws/status` on a changed revision, at most every 250 ms; `stackFree.roof`
- [x] T022 [P] [US4] `web/src/types/status.ts` (`roof`, `stackFree.roof`), `web/src/mocks/data.ts` (a closed roof), regenerate `specs/001-platform-bootstrap/contracts/schemas/status.schema.json` and `config.schema.json` from the demo samples

## Phase 7: User Story 5 - Clear a fault (P2)

- [x] T023 [US5] `POST /api/roof/clear-fault` (`requireAuth`, person origin) in `src/WebServerRoof.cpp`; the record is removed by T020's save path

## Phase 8: User Story 6 - Wire and calibrate the roof (P2)

- [x] T024 [P] [US6] Update `docs/hardware/wiring.md` (what the firmware reads, polarity, pull-ups on, PWM 20 kHz, the watchdog)
- [x] T025 [P] [US6] Write `docs/hardware/calibration.md` (Hall, supply, current scale) and add it to `mkdocs.yml`

## Phase 9: Polish

- [x] T026 [P] REST and WebSocket references (`docs/api/rest.md`, `websocket.md`), `docs/user-guide/roof-motion.md` (the control task, watchdog, fault persistence, gates), `docs/user-guide/configuration.md` (the new setting), `docs/development/heritage.md`, DIA-11 re-confirmed with the new library
- [x] T027 [P] `tools/api/routes.json`, `operations.json`, examples, `python3 tools/api/openapi.py`; `tools/i18n/gen_device_catalog.py` (the new API file) and `en.json`/`en.context.json`; i18n checks
- [x] T028 `bash tools/demo-core/build.sh` (ConfigModel changed); `tools/settings-deps/check.py`, `tools/dashboard/check.py`
- [x] T029 The full quickstart §1 (firmware build with zero warnings, native tests, web typecheck/test/build, Playwright, quality gate, flash budget, docs build)

## Dependencies

Phase 2 before everything else. T012-T014 before T015-T018. T019 before T020. T021 before T022.
Docs (T024-T026) any time after Phase 2.

## Phase 10: Review fixes

- [x] T030 Tests then `lib/RoofIoLogic/include/MotionGuards.h`, `src/MotionGuards.cpp`: `platformActionRefused`, `flashBusyRefuses`, `commandHeaderValid`, `watchdogAlive`, `runningAmps` (duty scaling, broken-wire NaN) in `test/test_roof_gates`; `FaultCause::WatchdogStopped` in `FaultRecord` and `RoofText` (FR-024 to FR-028)
- [x] T031 `CONFIG_ASYNC_TCP_PRIORITY=3` in `platformio.ini`; the comment in `include/RoofControl.h` and research R1 corrected (FR-025)
- [x] T032 Refuse settings saves, restores, uploads, WiFi changes and restarts while moving; hold scheduled restarts, command-line OTA, MQTT reconnects and the alerts pause NVS write; `flashBusy` refusal; `WiFi.persistent(false)` (FR-024)
- [x] T033 MQTT socket timeout 3 s and no reconnect while moving; hand-over limit re-derived in research R8
- [x] T034 Current read with 8 samples and scaled by the applied duty in the control step (FR-026)
- [x] T035 `X-RoofRoof: 1` on the roof routes (403), `roofCommand()` in `web/src/lib/api.ts` with a Vitest test, OpenAPI header parameter, REST and security docs (FR-027)
- [x] T036 Watchdog heartbeat: `MotorDriver::watchdogBeat()`, EN only with a fresh beat, `watchdogStopped` latched by the step (FR-028)
- [x] T037 Known limits in spec.md; roof-motion, wiring and calibration docs; status contract regenerated

## Phase 11: Second review fixes

- [x] T038 Tests then `RoofIo::FlashActivity` (stale timeout, replaced tokens, close stops it), `Blockers`/`blockerDecision`, `commandLineOtaAllowed`, `watchdogStoppedFault`, `runningAmps` with the `minRunningAmps` floor (FR-026, FR-028 to FR-030)
- [x] T039 `src/FlashGuard.cpp`; both upload routes track their flash activity per request (`_tempObject`), end it on disconnect, stop on a close or a move (FR-029, FR-031)
- [x] T040 `RoofControl` blockers and the waiting close (202, `closePending` in the status); `WebServer::restartScheduled/cancelRestart`; `MQTTClient::connecting()` (FR-029)
- [x] T041 Command-line OTA only while closed, counted as a flash write (FR-030)
- [x] T042 `MotorDriver::apply` reports a held EN; `applyOutput` latches `watchdogStopped` (FR-028)
- [x] T043 `roof.minRunningAmps` setting (model, schema, fixture, catalogue, tests, docs); wiring pull-up option; status contract, OpenAPI and docs

## Phase 12: Third review fixes

- [x] T044 Tests then `Roof::Controller::check()` (dry run of `command()`), used so a close's side effects and deferral happen only for a close that would run, never when closed or closing (FR-029)
- [x] T045 A refused open keeps a waiting close; a fault ends it (FR-029)
- [x] T046 MQTT connects held while a close waits, asked inside `MQTTClient::connect()` (FR-029)
- [x] T047 Watchdog beat limit 50 ms with tests; `maxWatchdogBeatAgeMs` in the status (FR-028)
- [x] T048 Command-line OTA re-checks the roof in its start callback (FR-030)
- [x] T049 Filesystem-upload restarts are mandatory (never cancelled by a close), and the restart timer waits while a close waits (FR-029)
