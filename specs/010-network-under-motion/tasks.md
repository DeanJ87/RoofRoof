# Tasks: Wi-Fi and OTA Under Motion

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: the pure rules serve US1-US3 together and come first; the firmware glue follows
by story. Every task ends with `pio test -e native` green; firmware tasks also with
`pio run -e esp32dev` and zero warnings.

## Phase 1: Tests of the rules (write first)

- [ ] T001 [US1] [US2] [US3] `test/test_platform_hold/test_main.cpp`: every action × every reason from contracts/platform-hold.md (moving in opening and closing; close waiting; sequence busy; all clear for each state at rest: closed, open, stopped, unknown, error); the order of reasons; the mandatory restart and flash write ignore the sequence; texts and names; `hotspotRefuses` for every command over the hotspot and the home network
- [ ] T002 [US1] `test/test_roof_gates/test_main.cpp`: `FlashActivity` short writes (count, never below zero, `writing()`, an upload's token unaffected by short writes, `busy()` upload-only); `RestartPlan::commit` (only when due; cancel refused once committed; cancellable false; clear resets; a schedule after commit keeps it committed); `blockerDecision` with `flashWriting` (open refused, close deferred without stopFlash, stop proceeds) and `restartCommitted` (open and close refused, stop and abort proceed); `blockerText` for both

## Phase 2: The rules

- [ ] T003 `lib/RoofIoLogic/include/PlatformHold.h`, `src/PlatformHold.cpp` until T001 passes
- [ ] T004 `lib/RoofIoLogic/include/MotionGuards.h`, `src/MotionGuards.cpp` until T002 passes; `bash tools/demo-core/build.sh`

## Phase 3: US1 - one decision point

- [ ] T005 [US1] `RoofControl::whileStill(action, wait)`; `command()` and `automaticClose()` probe blockers under the lock; `persistFault` holds a short write under the lock and skips while moving or a close waits (`include/RoofControl.h`, `src/RoofControl.cpp`)
- [ ] T006 [US1] `FlashGuard::holdShort/releaseShort/writing` (`include/FlashGuard.h`, `src/FlashGuard.cpp`)
- [ ] T007 [US1] `include/PlatformGuard.h`, `src/PlatformGuard.cpp`: `begin`, `state`, `hold`, `FlashWrite` (RAII), `beginUpload`, `commitRestart`
- [ ] T008 [US1] Web server: settings save, restore, Wi-Fi credentials save, alerts pause state through `PlatformGuard::FlashWrite`; uploads through `beginUpload` with the reason in the refusal; blockers probe adds `flashWriting` and `restartCommitted` (`src/WebServerApi.cpp`, `WebServerBackup.cpp`, `WebServerUpdates.cpp`, `WebServerRoof.cpp`, `WebServerAlerts.cpp` `saveArmedState` only)
- [ ] T009 [US1] Restart timer: hold by `PlatformGuard`, commit under the lock without waiting, retry after 100 ms when the lock is busy and 1 s when held; `RestartPlan` commit; remove `holdRestarts` (`src/WebServer.cpp`, `include/WebServer.h`, `src/main.cpp`)
- [ ] T010 [US1] MQTT connect hold synchronised on the roof's lock (`src/main.cpp`)
- [ ] T011 [US1] Command-line OTA: start check and flash activity atomic under the lock (closed, sequence idle); flash stays busy after a successful end (`src/main.cpp`)

## Phase 4: US2 - the automatic close isn't interrupted

- [ ] T012 [US2] `CloseOnUnsafe::busy()` (atomic phase, set in `begin` and `handle`); `PlatformGuard::begin(&roofControl, busy)` in `src/main.cpp`
- [ ] T013 [US2] `POST /api/restart`, a restore needing a restart, `POST /api/update`, `POST /api/wifi/connect` and `GET /api/wifi/scan` refused through `PlatformGuard::hold` with the reason's text

## Phase 5: US3 - hotspot

- [ ] T014 [US3] Roof open over the hotspot refused with reason `hotspot` (`src/WebServerRoof.cpp`; `arrivedViaHotspot` shared through `src/WebServerShared.h`)

## Phase 6: US4 - MQTT settings on the loop

- [ ] T015 [US4] `MQTTClient::updateConfig` hands over under a mutex; `handle()` applies on the loop; `getStatus()` copies under the mutex; enabled flag atomic (`include/MQTTClient.h`, `src/MQTTClient.cpp`)

## Phase 7: US5 - resets

- [ ] T016 [US5] Reset reason logged at start-up (`src/main.cpp`)

## Phase 8: Docs

- [ ] T017 [P] `docs/user-guide/roof-motion.md`: the hold table, short writes, committed restart, Wi-Fi loss and reset behaviour; `docs/user-guide/ota.md`, `docs/getting-started/first-setup.md` (hotspot can't open the roof), `docs/user-guide/mqtt.md` and `safety-inputs.md` (FR-011a limit resolved)
- [ ] T018 [P] `tools/api/operations.json` descriptions for the new 409s and `hotspot`; regenerate `docs/api/openapi.json`; `docs/api/rest.md`
- [ ] T019 [P] `docs/development/heritage.md` (MQTTClient, WebServer divergence; upstream candidate), `CHANGELOG.md`

## Phase 9: Verify

- [ ] T020 Deliberate breakage of each rule (quickstart §2), results recorded for the PR
- [ ] T021 Quickstart §1 of spec 001: firmware build (zero warnings), native tests, web typecheck/test/build, Playwright, quality gate, flash budget, docs build; demo core rebuilt
- [ ] T022 Independent safety reviews until one reports safe to merge; fixes with tests

## Dependencies

Phase 1 → Phase 2 → Phase 3 → Phases 4-7 (any order) → Phase 8 → Phase 9.

## Implementation strategy

The rules and the shared lock (Phases 1-3) are the core; everything after uses them.
