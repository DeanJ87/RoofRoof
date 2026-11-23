# Implementation Plan: Park Source and SQMeter Secrets

**Branch**: `spec/024-park-source-and-secrets` | **Date**: 2026-11-23 | **Spec**: [spec.md](spec.md)

## Summary

Part 1 adds `park.source` (N.I.N.A. only, park switches only, N.I.N.A. and switches) to the park
rule in `lib/SafetyInputLogic` (`ParkInputs::switchesOnly`), so the one park result every gate, the
interlock, the automatic close, jog, teach and the calibration run read changes in one place. With
switches only there is no N.I.N.A. ride-through and no freshness bound; a fresh slewing reading
still vetoes. A per-switch "switch watch" (roof openings since the switch last read open) in the
control task refuses Open at 3; it is stored in NVS beside the roof's fault, written only while the
roof is still. The test box's park bypass is refused with switches only. Constitution 5.0.0.

Part 2 reads SQMeter's `GET /api/secrets` on the device: in the copy job (right after
`/api/config`, only from the saved address) and in the poller (one pass after each good settings
read while the MQTT or alerts group follows). The copy holds the secrets in memory for 30 minutes
and answers only which were received; a ticked row sends the placeholder `(from SQMeter)`, which
`Config::applyJson` swaps for the held value when the field's server still matches (or refuses).
The sync fills a followed group's secrets and turns on SQMeter's on/off switches.

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 004's park rule, 021's close hold, 022's test box, 019's teach/jog gates, 020's SQMeter copy and sync, 023's pins |
| **Storage** | `park.source` in the settings JSON (NVS); the switch watch as a 2-byte NVS blob `parkWatch` in the `roofroof` namespace; secrets in memory only |
| **Testing** | `pio test -e native` (new `test_park_source`, `test_sqmeter_secrets`; extended park, test box, config, sync suites); Vitest (copy rows, park screens, schema); Playwright (demo copy with secrets) |
| **Target Platform** | ESP32 DevKit and WT32-ETH01, one image |
| **Project Type** | Embedded firmware + web UI + docs |
| **Constraints** | flash at 91.3% of the OTA slot: small additions, savings sought; settings copies on the heap (async_tcp stack) |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | No fault, timeout, watchdog or supply change; the switch watch is written only while the roof is still, from the control task as the fault is | `test_park_source` (gate), existing roof suites |
| II. Parked mount | **Amended (5.0.0, MAJOR)**: switches only, with the four guards; one park result for every path; with two switches both must read closed; unreadable is not parked | `test_park_source`, `test_park_logic`, `test_park_grace`, `test_test_box`, `test_roof_gates` |
| III. Unsafe ends closed | The request goes out only with an address; the wait and the alert read the park result | `test_park_source` (close sequence) |
| IV. Alpaca | Unchanged | ConformU in CI |
| V. Pure logic | `ParkRule`, `SwitchWatch`, `SqmeterSecrets`, `CopiedSecrets` fill, sync in `lib/`; tests first | native suites |
| VI. Budgets | small code; heap copies kept off async_tcp; flash measured | build, `flash_budget.py`, `size_check.py` |
| VII. UI | choice card group, Note, existing copy rows; Open's refusal says why | Vitest |
| VIII. Security | secrets never in a response; the placeholder only swapped for the server it came with; copy and save need auth; secrets only from the saved address | `test_sqmeter_secrets` |
| IX. Docs | safety inputs, wiring, commissioning, configuration, REST, wizard, SQMeter site; OpenAPI; contracts | `mkdocs build --strict` |
| X. Quality | check.py, function limits | check.py |
| XI. Heritage | `fetchJson` also parses a 403 body (RoofRoof-only module) | - |

**Gate**: pass with the MAJOR amendment stating the new risk.

## Project Structure

```text
specs/024-park-source-and-secrets/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
.specify/memory/constitution.md (5.0.0), specs/ROADMAP.md, CHANGELOG.md
lib/ConfigModel: Config.h (ParkConfig::Source, CopiedSecrets), ConfigJsonIn/Out, ConfigValidate, CopiedSecrets.cpp (new)
lib/SafetyInputLogic: ParkRule.h/.cpp, ParkHold.cpp, TestBox.cpp, SwitchWatch.h/.cpp (new), SqmeterSecrets.h/.cpp (new), SqmeterSite (switches, secrets part), SqmeterSync (secrets, switches)
lib/RoofIoLogic: RoofGates (switch check refusal), RoofStatus (watch), RoofText; SettingsGuard (source is a motion setting)
lib/SettingsDeps: park.source entries
src: SafetyInputs, RoofControl (+ watch, NVS), RoofFaultStore (watch blob), SafetyPoller (secrets read), SiteSync, WebServerSite (secrets in the copy, held), WebServerApi (fill), main
tools/demo-core: park source, watch, secrets in copy and sync, SQMeter's /api/secrets
web/src: types, schema, ParkFields (source), wizard park screens, sqmeterCopy rows, SqmeterCopy, i18n, mocks
docs: user-guide/safety-inputs.md, configuration.md, sqmeter site page, wizard; hardware/wiring.md, commissioning; api/rest.md; openapi.json; contracts
```

## Phases

1. Constitution, roadmap, spec docs.
2. Pure logic, tests first: park source and its settings, switch watch, gates, secrets parse, fill, copy document, sync.
3. Firmware glue and demo core.
4. Web UI and tests.
5. Docs, contracts, OpenAPI.
6. Verify (spec 001 quickstart §1), converge, review rounds until safe to merge.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| Principle II relaxed: switches may confirm the park without N.I.N.A. | Observatories whose mount has reliable park switches, or no N.I.N.A. running at night, can't close on them today | Requiring N.I.N.A. always: the owner's question is exactly why the switches can't be the truth; requiring two switches: turns away the one-switch case, which the stuck check covers for long-lived faults |

The relaxation is made by the MAJOR amendment itself (governance), not by an exception to it.
