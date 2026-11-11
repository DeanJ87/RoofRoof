# Tasks: Safety Inputs

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: US1 (weather verdict), US2 (park rule) and US3 (N.I.N.A. client) are pure logic
and independent; US4 (settings) is needed by the firmware glue; the firmware phase wires all four.
Every task ends with `pio test -e native` green; firmware tasks also with `pio run -e esp32dev`.

## Phase 1: Setup

- [ ] T001 Add `lib/SafetyInputLogic` and `lib/NinaLogic` (include/, src/) and their `-I` paths to `[env:native]` in `platformio.ini`

## Phase 2: User Story 3 - N.I.N.A. Advanced API (P1)

**Goal**: build v2 requests and parse v2 replies behind `Nina::Api`. **Independent test**: `pio test -e native -f test_nina`.

- [ ] T002 [P] [US3] Write `test/test_nina/fixtures.h` (v2 replies built from the spec and `Mount.cs`: info parked/unparked/slewing/disconnected, 409, 500, non-JSON, truncated, missing and wrong-typed fields; park "Parking", "Mount already parked", 409) and `test/test_nina/test_main.cpp` (requests, every parse outcome)
- [ ] T003 [US3] Implement `lib/NinaLogic/include/NinaApi.h` (`Request`, `MountReading`, `ParkReply`, `Api`, `ApiVersion`, `makeApi`, `unreachableReading`) and `lib/NinaLogic/src/NinaApiV2.cpp` (ArduinoJson filter for the large info reply) until T002 passes

## Phase 3: User Story 1 - Weather verdict (P1)

**Goal**: one verdict from every configured source. **Independent test**: `pio test -e native -f test_safety_input_logic`.

- [ ] T004 [P] [US1] Write `test/test_safety_input_logic/fixtures.h` (SQMeter's example, rain example, broken variants) and `test/test_safety_input_logic/test_main.cpp`: parsing, `assessSource` for each state and the `<=`/`>` stale boundary, the start-up window, MQTT availability, combination (rain kept when stale, bit 10, safe with flags, no source, contact unreadable), the exhaustive "never safe with a bad source" sweep (SC-004), and `describe` stating value and limit
- [ ] T005 [US1] Implement `lib/SafetyInputLogic/include/SqmeterReply.h` + `src/SqmeterReply.cpp` (JSON, `1`/`0`, `online`/`offline`)
- [ ] T006 [US1] Implement `lib/SafetyInputLogic/include/SafetyInput.h` + `src/SafetyInput.cpp` (`SourceRecord`, `assessSource`, `evaluateWeather`, `describe`) until T004 passes

## Phase 4: User Story 2 - Park rule (P1)

**Goal**: parked only with N.I.N.A. AND the fitted switches. **Independent test**: `pio test -e native -f test_park_logic`.

- [ ] T007 [P] [US2] Write `test/test_park_logic/test_main.cpp`: all-true parked for 0/1/2 switches; each failing input; freshness boundary; disagreement both ways; exhaustive sweep that parked implies every condition; `describe`
- [ ] T008 [US2] Implement `lib/SafetyInputLogic/include/ParkRule.h` + `src/ParkRule.cpp` until T007 passes

## Phase 5: User Story 4 - Settings (P2)

**Goal**: configure the sources, validated alike on device and web. **Independent test**: native config tests and Vitest.

- [ ] T009 [US4] Add `SafetyConfig` and `ParkConfig` to `lib/ConfigModel/include/Config.h`; defaults in `ConfigModel.cpp`; JSON out in `ConfigJsonOut.cpp`; JSON in (strings for `api` and `switches`, whole-number check for `staleSeconds`) in `ConfigJsonIn.cpp`; rules in `ConfigValidate.cpp` (URLs via `Net::parseHttpUrl`, `http://` only)
- [ ] T010 [P] [US4] Add `safety.staleSeconds` to `test/fixtures/config-ranges.json`; extend `test/test_config_model` (defaults, round trip, each validation message, 001 config loads with sources off)
- [ ] T011 [P] [US4] Web: `web/src/validation/configSchema.ts` (same rules and messages), `web/src/types`, mocks' config; Vitest for the schema
- [ ] T012 [US4] Settings dependency D-40 (`safety.mqtt.enabled` needs MQTT on, broker connected): `lib/SettingsDeps/catalogue.json`, `lib/SettingsDeps/src/SettingsDeps.cpp`, `web/src/lib/settingsDeps.ts`, `test/fixtures/settings-deps/*`, the settings fixtures' default config, `docs/reference/settings-dependencies.md` (generated), and `web/src/dashboard/inventory.json` (not shown: settings page is spec 009); run `tools/settings-deps/check.py` and `tools/dashboard/check.py`

## Phase 6: Firmware glue (US1-US3 on the board)

- [ ] T013 `MQTTClient::watch(topics, handler)`: absolute-topic subscriptions, re-subscribed on connect, replaced on config change (`include/MQTTClient.h`, `src/MQTTClient.cpp`)
- [ ] T014 `include/SafetyPoller.h`, `src/SafetyPoller.cpp`: FreeRTOS task (6 KB, priority 1, core 0) polling SQMeter every 5 s and N.I.N.A. every 2 s with 3 s timeouts, a park request flag, mutex-guarded snapshot; stack high-water logged at debug
- [ ] T015 `include/SafetyInputs.h`, `src/SafetyInputs.cpp`: MQTT records, 1 Hz evaluation of verdict and park result (rain contact and switches unreadable until spec 003 supplies them), info log on change, `requestPark()` for spec 005
- [ ] T016 Wire into `src/main.cpp` (start after MQTT, `updateConfig` on save, `handle` in the loop); `pio run -e esp32dev` with zero warnings; `tools/firmware/flash_budget.py --envs esp32dev`

## Phase 7: Polish

- [ ] T017 [P] Docs: `docs/user-guide/safety-inputs.md` (sources, fail-safe rules, N.I.N.A. setup, settings via API until 009), `docs/user-guide/configuration.md`, `docs/user-guide/mqtt.md` (subscribed topics), `mkdocs.yml` nav, `docs/development/heritage.md` (MQTTClient changed; new libs), `CHANGELOG.md`
- [ ] T018 Rebuild the demo core (`bash tools/demo-core/build.sh`); regenerate contract samples and `config.schema.json`; `python3 tools/api/openapi.py` if the config schema feeds it
- [ ] T019 Quickstart §1 of spec 001 in full; quality gate; docs build
- [ ] T020 `/speckit-converge` until converged; rebase onto `origin/main`; PR

## Dependencies

- T001 → everything. US3 (T002-T003), US1 (T004-T006), US2 (T007-T008) are independent; US2 uses `Nina::MountReading` from T003.
- US4 (T009-T012) is independent of the logic phases.
- T013-T016 need T003, T006, T008, T009.
- T017-T020 last.

## Parallel examples

- T002, T004 and T007 (three test files) can be written together.
- T010 and T011 together after T009.

## Implementation strategy

MVP = the three pure libraries with their tests (the contract spec 005 builds on), then settings,
then the firmware glue, then docs. Each phase is a commit `004: …`.
