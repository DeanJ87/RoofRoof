# Implementation Plan: Data Interfaces

**Branch**: `spec/008-data-interfaces` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/RoofLogic`: a fourth command origin, `Origin::Mqtt`, which is not a person (so spec 002
  refuses it a move from unknown and clearing a fault).
- `lib/RoofIoLogic`:
  - `CommandCheck`: the Alpaca client's rules apply to every remote origin (`remoteOrigin()`:
    Client and Mqtt); refusal texts name the source.
  - `RoofStatus`: the snapshot and the roof section gain the close phase, the weather verdict and
    its text, and whether the park is confirmed and why.
  - `RoofMqtt` (new): the command topic's payload parsing and gating (switched off, retained, not a
    command), the retained-flag reading from PubSubClient 2.8's receive buffer, the result
    document, the state document, and the publish schedule.
- `lib/HaDiscovery`: the roof's cover, sensors and fault binary sensor, with groups for roof
  commands and current sense.
- `lib/ConfigModel`: `mqtt.roofCommands` (default false) in, out and in the defaults.
- Firmware:
  - `MQTTClient`: several command subtopics, and the retained flag passed to command handlers.
    Nothing else changes there (spec 010 moves `updateConfig` and the reconnect hold).
  - `WebServer`: the command handler (`RoofControl::command(..., Origin::Mqtt)`), the result and
    state publishing, discovery held while the roof moves, the close phase in the status and in the
    WebSocket push trigger.
  - `RoofControl::snapshot()`: the gates as the checks see them now.
  - `CloseOnUnsafe::phase()`: the sequence's phase, readable from any task.
- Web: the setting in Settings → Network → MQTT (existing toggle and tip), types, zod schema,
  defaults, mocks (status and config), i18n.
- Registries and docs: config fixtures, settings-deps catalogue, OpenAPI (status example),
  contract schemas, the MQTT guide with DIA-09 redrawn, REST, WebSocket, configuration,
  integrations, close-on-unsafe, roof motion, heritage.

Decisions are in [research.md](research.md); the types in [data-model.md](data-model.md); the topics
and entities in [contracts/mqtt.md](contracts/mqtt.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | PubSubClient 2.8.0 (pinned), ArduinoJson 6, spec 006's `RoofIo::checkCommand`, spec 005's close sequence |
| **Storage** | one new boolean, `mqtt.roofCommands`, in the existing config JSON (default false) |
| **Testing** | `pio test -e native` (new: `test_roof_mqtt`; extended: `test_dome_bridge`, `test_dome_close`, `test_roof_logic`, `test_roof_status`, `test_ha_discovery`, `test_config_model`, `test_config_migration`, `test_device_core`), Vitest (schema, settings page), Playwright, contract schemas |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | a command runs within one main-loop pass of arriving; a state change reaches MQTT within 1 s |
| **Constraints** | nothing new blocks the main loop while the roof moves: no periodic state and no discovery publishes then; flash within SIZE-04; growth recorded if over 15 KB |
| **Scale/Scope** | ~350 lines of lib logic, ~600 lines of tests, ~150 lines of firmware glue, docs |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | MQTT commands use `Origin::Mqtt`, not a person: spec 002 refuses it a move from unknown and clearing a fault; there is no MQTT clear-fault, abort or set-position. Motion stays in the roof task; MQTT only calls `RoofControl::command`. | `test_roof_logic`: `test_mqtt_origin_*`; `test_roof_mqtt`: `test_only_three_commands` |
| II. Parked mount | A close from MQTT goes through `RoofControl::command`, the same check (`RoofIo::checkCommand`) and the same interlock as every other close. | `test_dome_bridge`: `test_remote_close_refused_unless_parked` |
| III. Unsafe ends closed | Open is gated on the weather. An MQTT stop is refused while a close runs or waits in weather that isn't safe and never counts as a person's stop; an MQTT open can't reverse a close; a retained command never acts. Nothing else received over MQTT reaches the roof or the sequence. Refusals carry the gate's text. | `test_dome_bridge`: `test_remote_*` for both remote origins; `test_dome_close`: the MQTT loops against the real sequence; `test_roof_mqtt`: `test_retained_*`, `test_switched_off_*`, `test_result_*` |
| IV. Alpaca | Unchanged: the Dome keeps `Origin::Client`; ConformU still runs in CI. | ConformU workflow |
| V. Pure logic | Parsing, gating, retained-flag reading, the documents and the schedule live in `lib/RoofIoLogic/RoofMqtt`; discovery in `lib/HaDiscovery`; native tests for each. | above, `test_ha_discovery` |
| VI. Budgets | No new task. The state document is ~1 KB in the 3 KB MQTT buffer. No periodic publish or discovery while moving. Flash measured. | `test_roof_mqtt`: `test_state_document_fits`; `flash_budget.py` |
| VII. UI | One toggle with a "?" tip in the existing MQTT card; no new parts or styles. | Vitest, Playwright |
| VIII. Security | Commands only on `<base>/roof/set` from the configured broker, only with the setting on, through the shared check. No new HTTP route. | `test_roof_mqtt`; `tools/api` checks |
| IX. Docs | MQTT guide and DIA-09, REST/WebSocket/configuration references, OpenAPI and contract schemas regenerated. | `diagrams.py`, `mkdocs build --strict`, contracts test |
| X. Quality | check.py; functions ≤ 60 lines, complexity ≤ 15. | check.py |
| XI. Heritage | `HaDiscovery` and `MQTTClient` diverge further from SQMeter (roof entities; several command topics and the retained flag, an upstream candidate). | heritage.md |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/008-data-interfaces/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/mqtt.md
└── checklists/requirements.md

lib/RoofLogic/include/RoofTypes.h, src/RoofTypes.cpp     Origin::Mqtt
lib/RoofIoLogic/include/CommandCheck.h, src/CommandCheck.cpp   remoteOrigin(), source-named texts
lib/RoofIoLogic/include/RoofStatus.h, src/RoofStatus.cpp       closePhase, weather, park in the section
lib/RoofIoLogic/include/RoofMqtt.h, src/RoofMqtt.cpp           (new) commands, retained flag, documents, schedule
lib/HaDiscovery/include/HaDiscovery.h, src/HaDiscovery.cpp     roof entities
lib/ConfigModel/include/Config.h, src/ConfigJsonIn.cpp, src/ConfigJsonOut.cpp, src/ConfigModel.cpp
include/MQTTClient.h, src/MQTTClient.cpp                       several command subtopics, retained flag
include/WebServer.h, src/WebServer.cpp, src/WebServerRoof.cpp, src/WebServerStatus.cpp
include/RoofControl.h, src/RoofControl.cpp                     gates in the snapshot
include/CloseOnUnsafe.h, src/CloseOnUnsafe.cpp, src/main.cpp   phase()
platformio.ini                                                 PubSubClient pinned to 2.8.0
test/test_roof_mqtt (new), test_dome_bridge, test_dome_close, test_roof_logic, test_roof_status,
  test_ha_discovery, test_config_model, test_device_core
web/src/components/settings/NetworkTab.tsx, types, validation, mocks, i18n, tests
test/fixtures/config-ranges.json, test/fixtures/settings-deps/*, lib/SettingsDeps/catalogue.json
tools/api/examples/status.json, docs/api/openapi.json, specs/001-platform-bootstrap/contracts/schemas
docs/user-guide/mqtt.md, configuration.md, close-on-unsafe.md, roof-motion.md, docs/api/*,
  docs/development/heritage.md, config.example.json
```

**Structure Decision**: the MQTT-specific roof logic sits in `RoofIoLogic` next to `CommandCheck`
and `RoofStatus`, which it uses; a separate library would only add include paths. `MQTTClient`
changes are kept to `onCommand`, `dispatch` and `subscribeAll`, away from the parts spec 010 edits.

## Phases

1. Spec, plan, research, data model, contract, tasks.
2. Pure logic with tests first: the origin, the check, the status fields, `RoofMqtt`, discovery,
   the setting.
3. Firmware glue.
4. Web setting and mocks, registries, docs, schemas.
5. Full local check, independent safety reviews, converge.

## Complexity Tracking

None.
