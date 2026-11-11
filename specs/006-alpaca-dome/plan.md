# Implementation Plan: Alpaca Dome

**Branch**: `spec/006-alpaca-dome` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/AlpacaLogic`: the Dome device in the router (`AlpacaDome.h`: shutter states, actions,
  answers; the Dome's `DeviceInfo`; the Dome members in `AlpacaRouter.cpp`), and two new `Backend`
  members, `domeStatus()` and `domeCommand()`.
- `lib/RoofIoLogic`: `DomeBridge`, the pure mapping between the roof and the Dome that the
  firmware and the simulator share: roof state to shutter state and slewing, Dome action to roof
  command, refusal text for Alpaca, and which stops count as a person's (a person's only); and
  `CommandCheck`, the gate / client-rule / motion-logic decision that `RoofControl::precheck` and
  the simulator both call.
- `lib/RoofLogic`: a third command origin, `Origin::Client`, treated as not a person.
- Firmware: `WebServer::AlpacaBackend` lists the Dome, reads the roof's snapshot and sends Dome
  commands through `RoofControl::command(..., Origin::Client)`, logging each one; a client's stop
  stops the roof but never stands spec 005's close down.
- `tools/alpaca-sim`: the Dome over the same router and bridge, with spec 002's controller driving
  the test plant (`test/roof_sim/RoofSim.h`), stepped on simulated time; options for unsafe and
  unparked.
- `.github/workflows/alpaca-conformance.yml`: ConformU conformance and Alpaca protocol against the
  simulator's Dome.
- Web: mocks and tests list the Dome; the "no device" texts now only describe the demo.
- Docs: the Alpaca guide with DIA-18 redrawn, REST/OpenAPI intro, the route registry's reason for
  `/api/v1`, README, docs home, workflow, heritage; spec 001's exception marked resolved.

Decisions are in [research.md](research.md); the types in [data-model.md](data-model.md); the Dome's
members in [contracts/alpaca-dome.md](contracts/alpaca-dome.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5; host clang/gcc for the simulator); TypeScript strict |
| **Primary Dependencies** | `lib/AlpacaLogic` router, spec 002's `Roof::Controller`, spec 003's `RoofControl` and gates, spec 005's person-stop count, ASCOM ConformU 4.x |
| **Storage** | none (no new setting) |
| **Testing** | `pio test -e native` (new: `test_alpaca_dome` router tests with a stub backend, `test_dome_bridge`; extended: `test_roof_logic` for the client origin, `test_alert_clients` for the Dome in the client watch), Vitest and Playwright (Alpaca page, dashboard tile), ConformU locally and in CI |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB); the simulator on Linux/macOS |
| **Project Type** | Embedded firmware + Alpaca simulator + CI workflow + web mocks + docs |
| **Performance Goals** | a Dome command returns within one roof lock (as the web UI's); a status read copies the snapshot |
| **Constraints** | no blocking on the async TCP task beyond the roof lock the web UI already takes; flash within SIZE-04; growth recorded if over 15 KB |
| **Scale/Scope** | ~250 lines of lib logic, ~600 lines of tests, ~150 lines of firmware glue, ~150 lines of simulator |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | Dome commands use `Origin::Client`, which spec 002 refuses for a move from unknown and for clearing a fault (anything not `Person`). Open and close need a connection; abort is never refused (not even with Alpaca off) and stops with the estimate kept. Disconnect and silence change nothing. | `test_dome_bridge`: `test_client_*`, `test_stops_pass_every_check`, `test_a_stop_is_never_refused`; `test_alpaca_dome`: `test_open_and_close_need_a_connection`, `test_abort_*` |
| II. Parked mount | `CloseShutter` goes through `RoofControl::command`, the same function as the web UI's close: the park gate at the start, the interlock throughout. The decision is `RoofIo::checkCommand`, which the simulator calls too. | `test_dome_bridge`: `test_client_close_refused_unless_parked`; simulator run unparked (quickstart) |
| III. Unsafe ends closed | `OpenShutter` passes the weather gate; refusals carry the gate's text. Nothing a client sends can call the automatic close off: its stop isn't a person's stop, and its open can't reverse a close. | `test_dome_bridge`: `test_client_open_refused_while_unsafe`, `test_client_cannot_reverse_a_close`, `test_only_a_person_s_stop_counts`, `test_refusal_text_*` |
| IV. Alpaca | One router for firmware, simulator and demo. Interface version 3; shutter members implemented, the rest false or NotImplemented. ConformU in CI against the simulator. UniqueID unchanged in form. | `test_alpaca_dome` (every member), ConformU workflow |
| V. Pure logic | The mapping and the counting rule live in `lib/RoofIoLogic/DomeBridge`; the members in `lib/AlpacaLogic`; native tests at each state. | above |
| VI. Budgets | No new task, no new allocation per step; flash measured. | build, `flash_budget.py` |
| VII. UI | No new parts: the Alpaca page and dashboard tile already render any device. Text changes only. | Vitest, Playwright |
| VIII. Security | `/api/v1` stays open (Alpaca by design) and moves the roof only through the checked path; the route registry says so. | `tools/api` checks |
| IX. Docs | Alpaca guide and DIA-18 updated; references and home page. | `diagrams.py`, `mkdocs build --strict` |
| X. Quality | check.py; functions ≤ 60 lines, complexity ≤ 15. | check.py |
| XI. Heritage | `AlpacaRouter` (copied from SQMeter) gains the Dome: recorded as diverged in heritage.md. | - |

**Gate**: pass, no exceptions. Spec 001's ConformU exception is resolved by this spec.

## Project Structure

```text
specs/006-alpaca-dome/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/alpaca-dome.md
└── checklists/requirements.md

lib/AlpacaLogic/include/AlpacaDome.h        ShutterState, DomeStatus, DomeAction, DomeAnswer, domeDeviceInfo()
lib/AlpacaLogic/include/AlpacaRouter.h      Backend::domeStatus(), Backend::domeCommand()
lib/AlpacaLogic/include/AlpacaProtocol.h    ALPACA_ERR_INVALID_OPERATION
lib/AlpacaLogic/src/AlpacaDome.cpp          the Dome members (called from the router)
lib/AlpacaLogic/src/AlpacaRouter.cpp        dispatch to the Dome
lib/RoofIoLogic/include/DomeBridge.h, src/DomeBridge.cpp, include/CommandCheck.h, src/CommandCheck.cpp
lib/RoofLogic/include/RoofTypes.h           Origin::Client
include/WebServer.h, src/WebServerAlpaca.cpp   the Dome in the backend
src/RoofControl.cpp                          count a client's stops (DomeBridge rule)
tools/alpaca-sim/main.cpp, build.sh          the simulated Dome
tools/demo-core/emulated_device.h            the new Backend members (no device)
.github/workflows/alpaca-conformance.yml
test/test_alpaca_dome, test/test_dome_bridge, test/test_roof_logic, test/test_alpaca_logic
web/src/mocks/*, web/src/i18n/en*.json, web tests, Playwright specs
docs/user-guide/alpaca.md, docs/api/*, README.md, docs/index.md, docs/llms.txt, docs/development/*
tools/api/routes.json, tools/api/openapi.py, docs/api/openapi.json
specs/001-platform-bootstrap/plan.md        exception marked resolved
```

**Structure Decision**: the Alpaca side stays free of roof types (the demo compiles it without the
roof); the roof side of the mapping sits in `RoofIoLogic`, which both the firmware and the
simulator link, so neither has its own copy (constitution IV).

## Phases

1. Spec, plan, research, data model, contract, tasks.
2. Pure logic with tests first: `Origin::Client`, `DomeBridge`, the Dome members.
3. Firmware glue, simulator, ConformU locally, workflow.
4. Web mocks and texts, docs, registries, heritage.
5. Full local check, independent safety reviews, converge.

## Complexity Tracking

None.
