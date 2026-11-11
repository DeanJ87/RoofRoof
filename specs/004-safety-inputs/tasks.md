# Tasks: Safety Inputs

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: US1 (weather verdict), US2 (park rule) and US3 (N.I.N.A. client) are pure logic
and independent; US4 (settings) is needed by the firmware glue; the firmware phase wires all four.
Every task ends with `pio test -e native` green; firmware tasks also with `pio run -e esp32dev`.

## Phase 1: Setup

- [x] T001 Add `lib/SafetyInputLogic` and `lib/NinaLogic` (include/, src/) and their `-I` paths to `[env:native]` in `platformio.ini`

## Phase 2: User Story 3 - N.I.N.A. Advanced API (P1)

**Goal**: build v2 requests and parse v2 replies behind `Nina::Api`. **Independent test**: `pio test -e native -f test_nina`.

- [x] T002 [P] [US3] Write `test/test_nina/fixtures.h` (v2 replies built from the spec and `Mount.cs`: info parked/unparked/slewing/disconnected, 409, 500, non-JSON, truncated, missing and wrong-typed fields; park "Parking", "Mount already parked", 409) and `test/test_nina/test_main.cpp` (requests, every parse outcome)
- [x] T003 [US3] Implement `lib/NinaLogic/include/NinaApi.h` (`Request`, `MountReading`, `ParkReply`, `Api`, `ApiVersion`, `makeApi`, `unreachableReading`) and `lib/NinaLogic/src/NinaApiV2.cpp` (ArduinoJson filter for the large info reply) until T002 passes

## Phase 3: User Story 1 - Weather verdict (P1)

**Goal**: one verdict from every configured source. **Independent test**: `pio test -e native -f test_safety_input_logic`.

- [x] T004 [P] [US1] Write `test/test_safety_input_logic/fixtures.h` (SQMeter's example, rain example, broken variants) and `test/test_safety_input_logic/test_main.cpp`: parsing, `assessSource` for each state and the `<=`/`>` stale boundary, the start-up window, MQTT availability, combination (rain kept when stale, bit 10, safe with flags, no source, contact unreadable), the exhaustive "never safe with a bad source" sweep (SC-004), and `describe` stating value and limit
- [x] T005 [US1] Implement `lib/SafetyInputLogic/include/SqmeterReply.h` + `src/SqmeterReply.cpp` (JSON, `1`/`0`, `online`/`offline`)
- [x] T006 [US1] Implement `lib/SafetyInputLogic/include/SafetyInput.h` + `src/SafetyInput.cpp` (`SourceRecord`, `assessSource`, `evaluateWeather`, `describe`) until T004 passes

## Phase 4: User Story 2 - Park rule (P1)

**Goal**: parked only with N.I.N.A. AND the fitted switches. **Independent test**: `pio test -e native -f test_park_logic`.

- [x] T007 [P] [US2] Write `test/test_park_logic/test_main.cpp`: all-true parked for 0/1/2 switches; each failing input; freshness boundary; disagreement both ways; exhaustive sweep that parked implies every condition; `describe`
- [x] T008 [US2] Implement `lib/SafetyInputLogic/include/ParkRule.h` + `src/ParkRule.cpp` until T007 passes

## Phase 5: User Story 4 - Settings (P2)

**Goal**: configure the sources, validated alike on device and web. **Independent test**: native config tests and Vitest.

- [x] T009 [US4] Add `SafetyConfig` and `ParkConfig` to `lib/ConfigModel/include/Config.h`; defaults in `ConfigModel.cpp`; JSON out in `ConfigJsonOut.cpp`; JSON in (strings for `api` and `switches`, whole-number check for `staleSeconds`) in `ConfigJsonIn.cpp`; rules in `ConfigValidate.cpp` (URLs via `Net::parseHttpUrl`, `http://` only)
- [x] T010 [P] [US4] Add `safety.staleSeconds` to `test/fixtures/config-ranges.json`; extend `test/test_config_model` (defaults, round trip, each validation message, 001 config loads with sources off)
- [x] T011 [P] [US4] Web: `web/src/validation/configSchema.ts` (same rules and messages), `web/src/types`, mocks' config; Vitest for the schema
- [x] T012 [US4] Settings dependency D-40 (`safety.mqtt.enabled` needs MQTT on, broker connected): `lib/SettingsDeps/catalogue.json`, `lib/SettingsDeps/src/SettingsDeps.cpp`, `web/src/lib/settingsDeps.ts`, `test/fixtures/settings-deps/*`, the settings fixtures' default config, `docs/reference/settings-dependencies.md` (generated), and `web/src/dashboard/inventory.json` (not shown: settings page is spec 009); run `tools/settings-deps/check.py` and `tools/dashboard/check.py`

## Phase 6: Firmware glue (US1-US3 on the board)

- [x] T013 `MQTTClient::watch(topics, handler)`: absolute-topic subscriptions, re-subscribed on connect, replaced on config change (`include/MQTTClient.h`, `src/MQTTClient.cpp`)
- [x] T014 `include/SafetyPoller.h`, `src/SafetyPoller.cpp`: FreeRTOS task (6 KB, priority 1, core 0) polling SQMeter every 5 s and N.I.N.A. every 2 s with 3 s timeouts, a park request flag, mutex-guarded snapshot; stack high-water logged at debug
- [x] T015 `include/SafetyInputs.h`, `src/SafetyInputs.cpp`: MQTT records, 1 Hz evaluation of verdict and park result (rain contact and switches unreadable until spec 003 supplies them), info log on change, `requestPark()` for spec 005
- [x] T016 Wire into `src/main.cpp` (start after MQTT, `updateConfig` on save, `handle` in the loop); `pio run -e esp32dev` with zero warnings; `tools/firmware/flash_budget.py --envs esp32dev`

## Phase 7: Polish

- [x] T017 [P] Docs: `docs/user-guide/safety-inputs.md` (sources, fail-safe rules, N.I.N.A. setup, settings via API until 009), `docs/user-guide/configuration.md`, `docs/user-guide/mqtt.md` (subscribed topics), `mkdocs.yml` nav, `docs/development/heritage.md` (MQTTClient changed; new libs), `CHANGELOG.md`
- [x] T018 Rebuild the demo core (`bash tools/demo-core/build.sh`); regenerate contract samples and `config.schema.json`; `python3 tools/api/openapi.py` if the config schema feeds it
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

## Phase 8: Convergence

Findings from the first convergence pass (independent review of the code against the spec, plan
and constitution).

- [x] T021 [CRITICAL, F1, constitution III] Times in `lib/SafetyInputLogic` become 64-bit milliseconds that never wrap (firmware: `esp_timer_get_time() / 1000`); a timestamp after `now` still reads age 0; native tests for ages past 2^31 and 2^32 ms in the weather and park rules
- [x] T022 [HIGH, F2, constitution VI] `SafetyInputs::updateConfig` (called from the web server's task) only hands the new settings over under a lock; the main loop applies them, so `MQTTClient::watch` and every record change run on the loop
- [x] T023 [HIGH, F3, constitution V] Move "apply an MQTT message to the record" and "record an HTTP reply" into `lib/SafetyInputLogic` (`applyMqttMessage`, `recordHttpReply`) with native tests (rain JSON then `safe` 1 keeps rain; unknown topic; bad payloads; status classes)
- [x] T024 [MEDIUM, F4, FR-011] After each (re)subscription, the first `safe`/`safety` message of each topic (the broker's retained copy) may only make the source less safe: a retained "safe" is ignored until a live one arrives; tests in lib
- [x] T025 [MEDIUM, F5, FR-012] `setRainContact` / `setParkSwitches` take `Safety::RainContact` / `Safety::SwitchReading`, so spec 003 can report "unreadable"
- [x] T026 [MEDIUM, F6, FR-010/FR-018] Bound every poll: a total deadline per request and a body cap (8 KB), read straight into `std::string`; document the worst case
- [x] T027 [LOW, F7] A good reply already older than the limit is stale even inside the start-up window; test
- [x] T028 [LOW, F8, US3/AC3] Align the spec with the parser: `Success: false` is refused with N.I.N.A.'s text; any other unexpected answer is unreadable
- [x] T029 [LOW, F9] Restart a source's records and window only when its own settings change; tests via the poller targets comparison
- [x] T030 [LOW, F10] Log changes by comparing the verdict fields directly, without building key strings each second
- [ ] T031 [LOW, F11] T019-T020: quickstart §1, quality gate, docs build; bench figures go in the PR's pending bench checks

## Phase 9: Convergence

Second convergence pass, on the Phase 8 fixes.

- [x] T032 [HIGH, N1, constitution VI] An empty reply leaves HTTPClient without a stream: treat it as an empty body (unreadable), never dereference it (`src/SafetyPoller.cpp`)
- [x] T033 [MEDIUM, N2, FR-010] State the real bound in FR-010, the plan and the code: the body by 6 s, header lines 3 s each, DNS unbounded; fail-safe because readings go stale
- [x] T034 [LOW, N3, T029] Move "which records a save restarts" into `lib/SafetyInputLogic` (`settingsChange`) with native tests (`test_safety_settings`); the poller and SafetyInputs only apply it
- [x] T035 [LOW, N4, FR-011a] Record the known limit (an MQTT settings save reconnects on the web task) in the spec and the PR

## Phase 10: Convergence

- [x] T036 [LOW, P1, T033] Spec assumption 4 states the same request bound as FR-010
