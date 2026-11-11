# Tasks: Data Interfaces

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: the pure logic first (origin and check, status fields, `RoofMqtt`, discovery, the
setting), then the firmware glue, then web, registries, docs and the full check. Every task ends
with `pio test -e native` green; firmware tasks also with `pio run -e esp32dev`.

## Phase 1: Setup

- [ ] T001 Pin PubSubClient to 2.8.0 in `platformio.ini` (research R2)

## Phase 2: Tests first

- [ ] T002 [US2] `test/test_roof_logic`: `Origin::Mqtt` refused a move from unknown (open and close) and a fault clear; accepted from a known position; `check()` agrees with `command()`
- [ ] T003 [US2] `test/test_dome_bridge`: every remote-client rule for both `Client` and `Mqtt` (stop refused while closing or a close waits and the weather isn't safe, whoever started it; stop accepted while safe; open refused while closing, waiting or stopped while closing; weather and park gates; unknown position; fault); `remoteOrigin()`; `countsAsPersonStop` false for `Mqtt`; refusal texts name MQTT for `Mqtt`
- [ ] T004 [US2] `test/test_dome_close`: the close/stop loop and the waiting-close case with `Origin::Mqtt`, plus an MQTT open/stop loop, against the real close sequence: the roof still ends closed and no MQTT stop counts as a person's
- [ ] T005 [US1][US2] `test/test_roof_mqtt/test_main.cpp`: `parseCommand` (case, whitespace, unknown and long payloads, the gate order switched off → retained → payload); `deliveryOf` against PubSubClient 2.8 buffers built byte for byte (QoS 0 and 1, retained and live, a 2-byte remaining length, and each corrupted check → Unknown); `writeResult`; `writeState` (time 0 until valid, the roof object equal to `writeStatus`'s); `StateSchedule` (first publish, new connection, key change after 250 ms not before, interval at rest, nothing periodic while moving); the worst-case state document under 1.5 KB
- [ ] T006 [US3] `test/test_roof_status`: `closePhase`, `weather`, `weatherText`, `parkConfirmed`, `parkText` written for each value
- [ ] T007 [US1] `test/test_ha_discovery`: the roof entities (cover with and without the command topic and its state template; the four sensors with units and classes; the motor current only with current sense; the fault binary sensor with its attributes); removal when a group is off; unique ids stable
- [ ] T008 `test/test_config_model` / `test_config_migration`: `mqtt.roofCommands` defaults to false, round-trips, and an older config without it loads false

## Phase 3: Pure logic

- [ ] T009 `Roof::Origin::Mqtt` (`RoofTypes.h`), until T002 passes
- [ ] T010 `RoofIo::remoteOrigin()` and the source-named texts in `CommandCheck`, until T003 and T004 pass
- [ ] T011 Snapshot fields and `writeStatus` keys in `RoofStatus`, until T006 passes
- [ ] T012 `lib/RoofIoLogic/include/RoofMqtt.h`, `src/RoofMqtt.cpp`, until T005 passes
- [ ] T013 `lib/HaDiscovery`: the entity table and payload builder, until T007 passes
- [ ] T014 `MQTTConfig::roofCommands` in `Config.h`, defaults, JSON in and out, until T008 passes
- [ ] T015 Deliberate-breakage run (outside the repo): at least 8 breakages of the MQTT rules (remote origin, stop refusal, open refusal, person-stop counting, retained gate, switched-off gate, the header checks, the schedule while moving), each must make a test fail; record the result in the PR

## Phase 4: Firmware

- [ ] T016 `MQTTClient`: several command subtopics; the retained flag (`RoofIo::Mqtt::deliveryOf`) passed to command handlers; the alerts switch handler unchanged in effect
- [ ] T017 `RoofControl::snapshot()` adds the gates as the checks see them now
- [ ] T018 `CloseOnUnsafe::phase()` (atomic); `WebServer::setClosePhase` wired in `main.cpp`; the status and the WebSocket push trigger use it
- [ ] T019 `WebServer`: subscribe `roof/set`; run accepted commands through `RoofControl::command(..., Origin::Mqtt)` and log each; queue and publish results; publish `<base>/state` on the schedule; hold discovery while the roof moves; discovery groups from `mqtt.roofCommands` and current sense
- [ ] T020 `Core::STATUS_DOC_CAPACITY` and `test_device_core`'s status-fits test with the new keys; build with zero warnings; flash budget

## Phase 5: Web

- [ ] T021 [US2] `mqtt.roofCommands` in `types/config.ts`, `validation/configSchema.ts`, defaults, `mocks/config.ts`; the toggle and its "?" tip in `NetworkTab.tsx`; `en.json` and `en.context.json`; Vitest for the toggle saving
- [ ] T022 [US3] The new roof keys in `types/status.ts` and `mocks/data.ts`; contract samples and schemas regenerated

## Phase 6: Registries and docs

- [ ] T023 `test/fixtures/settings-deps/default-config.json`, `config-ranges.json` if it lists booleans, `lib/SettingsDeps/catalogue.json` (`noDependency`), `config.example.json`; `tools/settings-deps/check.py`, `tools/dashboard/check.py`
- [ ] T024 `tools/api/examples/status.json`, `operations.json` if it describes the status; `python3 tools/api/openapi.py`
- [ ] T025 `docs/user-guide/mqtt.md` (topics, commands and their limits, Home Assistant entities, DIA-09 redrawn and re-confirmed), `docs/api/websocket.md`, `docs/api/rest.md`, `docs/api/integrations.md`, `docs/user-guide/configuration.md`, `close-on-unsafe.md`, `roof-motion.md`, `docs/development/heritage.md`, `docs/llms.txt` if it lists topics
- [ ] T026 `bash tools/demo-core/build.sh`; i18n checks; quality gate

## Phase 7: Check and review

- [ ] T027 Spec 001 quickstart §1 in full; record the numbers
- [ ] T028 Independent safety reviews until one says safe to merge; fix and re-test each finding
