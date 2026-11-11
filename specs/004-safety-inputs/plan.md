# Implementation Plan: Safety Inputs

**Branch**: `spec/004-safety-inputs` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

Two pure decisions and the plumbing that feeds them:

- `lib/SafetyInputLogic`: SQMeter reply parsing, per-source assessment (fresh, stale, failed,
  offline, waiting), the weather verdict (safe / unsafe with reasons / unknown) and the park rule
  (N.I.N.A. AND the fitted switches, disagreement is not parked), each with a text form that states
  the value and the limit.
- `lib/NinaLogic`: the N.I.N.A. Advanced API behind `Nina::Api` (builds requests, parses replies);
  `Nina::ApiV2` is the only implementation.
- Firmware: `SafetyPoller` (its own FreeRTOS task polling SQMeter over HTTP and N.I.N.A., and
  sending a park on request), `SafetyInputs` (MQTT topics, the 1 Hz evaluation and the change log),
  and absolute-topic subscriptions in `MQTTClient`.
- Settings `safety` and `park` through ConfigModel, ConfigStore (same document), the web Zod schema,
  the range fixture and the settings-dependency catalogue.

Decisions and sources are in [research.md](research.md); types in [data-model.md](data-model.md);
the consumed and exposed contracts in [contracts/](contracts/).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict; Python 3.12 |
| **Primary Dependencies** | ArduinoJson 6 (parsing, with a filter for N.I.N.A.'s large info reply), HTTPClient + `DualStackClient`, PubSubClient 2.8, FreeRTOS |
| **Storage** | NVS: two new sections in the main config document (`configB`), about 300 bytes at most |
| **Testing** | `pio test -e native` (new: `test_safety_input_logic`, `test_park_logic`, `test_nina`; extended: `test_config_model`, `test_config_ranges`, `test_settings_deps`), Vitest (config schema, settings deps) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI (settings schema only) + docs |
| **Performance Goals** | SQMeter HTTP polled every 5 s; N.I.N.A. every 2 s; verdict evaluated every 1 s; each HTTP request: 3 s connect and reply timeouts, done within 6 s, body ≤ 8 KB |
| **Constraints** | No blocking network calls on the loop or the async TCP task (VI); poller stack 6 KB with > 2 KB headroom measured on the bench; image < 75% of the OTA slot |
| **Scale/Scope** | ~900 lines of lib logic and tests, ~400 lines of firmware glue |

## Constitution Check

| Principle | How this plan satisfies it |
|---|---|
| I. Motion Safety | No motion code touched. The poller task never touches the driver pins; it runs on core 0 at priority 1 and can't delay the motion task (spec 003). |
| II. Parked mount | `Safety::evaluatePark` returns parked only for a fresh (≤ 10 s) N.I.N.A. reading that is connected, `AtPark`, not slewing, AND every fitted switch closed. Disagreement, an unreadable switch, an unreachable or unparseable N.I.N.A. are each "not parked" with a reason and a `disagree` flag for 005's critical alert. Native tests cover each combination and the `>` boundary. |
| III. Unsafe ends closed | Any source unsafe, rain (SQMeter bit 9, contact) or rain-sensor fault (bit 10) → unsafe; stale, unreachable, unreadable, offline and lost → unsafe; no weather source → unsafe; unknown only in the start-up window and never safe. Rain is kept as a reason even when its source has gone stale. Every reason states the value and limit or the failed input. Reporting (logs now; status/MQTT/UI in 008/009) can't change the verdict: the verdict is computed from inputs only. |
| IV. Alpaca | Untouched. |
| V. Pure logic | All decisions in `lib/SafetyInputLogic` and `lib/NinaLogic`, time passed in. Native tests for every rule and boundary, recorded N.I.N.A. and SQMeter fixtures. |
| VI. Budgets | Outgoing HTTP on its own task, plain `http://` only (no TLS session). Zero warnings; flash measured. The extra heap is the 6 KB task stack and one reply body at a time. |
| VII. UI | No UI in this spec (settings page is 009). Zod schema only. |
| VIII. Security | No new endpoint. The N.I.N.A. v2 API has no credentials; the interface carries Basic-auth fields for v3, to be masked when v3 adds them. URLs and topics aren't secrets. |
| IX. Docs | New user page `docs/user-guide/safety-inputs.md`; configuration and MQTT references updated; heritage updated for `MQTTClient`. No diagram changes (state machine and close sequence are 002/005). |
| X. Quality | check.py must pass; functions ≤ 60 lines, ≤ 5 parameters (inputs grouped in structs). |
| XI. Heritage | `MQTTClient` changed (absolute-topic subscriptions) — recorded. New libs are RoofRoof-only. |

**Gate**: pass, no exceptions.

## Project Structure

### Documentation (this feature)

```text
specs/004-safety-inputs/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md, tasks.md
├── contracts/ (sqmeter-safety.md, nina-api-v2.md, settings.md)
└── checklists/requirements.md
```

### Source code

```text
lib/SafetyInputLogic/
  include/SafetyInput.h      weather source records, assessment, verdict, describe
  include/SqmeterReply.h     parse /api/safety JSON and the MQTT payloads
  include/ParkRule.h         the park rule and its describe
  src/*.cpp
lib/NinaLogic/
  include/NinaApi.h          Nina::Api interface, Request, MountReading, ParkReply, makeApi
  src/NinaApiV2.cpp
lib/ConfigModel/             SafetyConfig, ParkConfig: defaults, JSON in/out, validation
lib/SettingsDeps/            D-40: safety.mqtt.enabled needs MQTT on and the broker connected
include/SafetyPoller.h, src/SafetyPoller.cpp     task: SQMeter HTTP + N.I.N.A. polls, park request
include/SafetyInputs.h, src/SafetyInputs.cpp     MQTT topics, 1 Hz evaluation, change log
include/MQTTClient.h, src/MQTTClient.cpp         watch(topics, handler): absolute subscriptions
src/main.cpp                                     start and feed SafetyInputs
test/test_safety_input_logic, test/test_park_logic, test/test_nina (+ fixtures)
web/src/validation/configSchema.ts, web/src/types, web/src/lib/settingsDeps.ts, mocks
test/fixtures/config-ranges.json, test/fixtures/settings-deps/*
docs/user-guide/safety-inputs.md, docs/user-guide/configuration.md, docs/user-guide/mqtt.md,
docs/development/heritage.md, mkdocs.yml
```

**Structure Decision**: the existing firmware + lib + web layout; two new pure libraries so the
N.I.N.A. protocol (which v3 will replace) is separate from the safety rules (which won't change).

## Phases

1. **Pure logic first (tests first)**: SQMeter parsing, source assessment and verdict; park rule;
   N.I.N.A. v2 interface with fixtures.
2. **Settings**: ConfigModel sections, validation, range fixture, config tests; web Zod schema and
   types, mocks; settings-dependency entry D-40 (catalogue, lib, web, cases, docs page).
3. **Firmware**: MQTTClient `watch`; SafetyPoller task; SafetyInputs; main.cpp wiring; build with
   zero warnings; flash budget.
4. **Docs and heritage**: user page, configuration and MQTT references, heritage, CHANGELOG.
5. **Verify**: quickstart §1 of spec 001, demo core rebuild, contract schemas regenerated (config),
   `/speckit-converge`.

## Complexity Tracking

None.
