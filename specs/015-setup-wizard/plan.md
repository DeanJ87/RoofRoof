# Implementation Plan: Setup Wizard

**Branch**: `spec/015-setup-wizard` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/ConfigModel`: `SetupConfig` (`setup.state`, `setup.step`), JSON in and out, validation,
  defaults, and the "stored before setup" migration to `offered`.
- `lib/SafetyInputLogic`: `Safety::InputsReport` and its JSON writer.
- Firmware: `Config::load` marks old stored settings `offered`; `SafetyInputs` keeps the report
  (once a second, own mutex) with an MQTT generation; `WebServer` serves `GET /api/roof/inputs`.
- Demo core: the same report from the simulated SQMeter and N.I.N.A.; the demo starts `done` and
  its panel restarts the wizard.
- Web: `useConfigEditor` (Settings' load, validation and save, shared), the `/wizard` page with ten
  pages built from the Settings cards and controls, the Test panel, the review, the dashboard
  prompt and banner, the System page's Setup card, i18n, mocks.
- Route registry, OpenAPI, contract schemas; docs (a Setup wizard guide, first boot,
  commissioning, configuration, REST, dashboard, live demo), CHANGELOG, roadmap.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the new
endpoint in [contracts/roof-inputs.md](contracts/roof-inputs.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | spec 004's `SafetyInputs` and `SafetyPoller`; spec 009's Settings cards and `HallScale`; spec 007's alert test; spec 010's `PlatformGuard::FlashWrite`; spec 011's demo core |
| **Storage** | NVS: one small section in the main config document |
| **Testing** | `pio test -e native` (new `test_inputs_report`, `test_config_setup`; `test_config_migration` extended); Vitest (review checks, Test logic, wizard pages, prompt); Playwright (`tests/wizard.spec.ts`, axe at phone width; screenshots) |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Project Type** | Embedded firmware + web UI + docs |
| **Performance Goals** | the report copied once a second on the main loop; the handler only copies |
| **Constraints** | no blocking on the async task; flash within SIZE-04 (growth over 15 KB recorded); UI growth under SIZE-01's 10 KB or recorded |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | No motion code changes. Wizard saves use the existing save path; `PlatformGuard::FlashWrite` refuses a save while the roof moves or a close waits. The first-run values only lower the top duty. | `test_platform_hold` (unchanged); Playwright: save refused while moving |
| II. Parked mount | Untouched. The review only reports the park result. | - |
| III. Unsafe ends closed | Untouched. The inputs document reports the verdict and can't change it (GET, copies only). | `test_inputs_report` |
| IV. Alpaca | Untouched; the wizard toggles `alpaca.enabled` (restart note as today). | ConformU CI |
| V. Pure logic | The migration, setup parsing and the report writer in `lib/` with native tests; review and Test logic in Vitest. | `test_config_setup`, `test_inputs_report`, Vitest |
| VI. Budgets | The report is built once a second on the main loop; the handler copies under a mutex and writes ~1 KB; no outgoing request. Flash and UI measured. | build, `flash_budget.py`, `size_check.py` |
| VII. UI | Settings cards, controls, tokens; no new button or card styles; i18n; a11y; phone width; Stop bar from the layout. | Vitest, Playwright + axe, `copy_check.py`, `label_check.py` |
| VIII. Security | Saves keep `requireAuth` and the masked secrets; the new endpoint is GET, open, readings only, no address or secret. | `test_inputs_report` (no URL in the output), `routes.py` |
| IX. Docs | Setup wizard guide, first boot, commissioning, configuration, REST; OpenAPI regenerated. | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py, function limits. | check.py |
| XI. Heritage | No SQMeter module copied or changed. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/015-setup-wizard/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
lib/ConfigModel/ Config.h, ConfigInternal.h, ConfigJsonIn.cpp, ConfigJsonOut.cpp, ConfigModel.cpp, ConfigValidate.cpp
lib/SafetyInputLogic/ include/InputsReport.h, src/InputsReport.cpp   (new)
src/ConfigStore.cpp, include/SafetyInputs.h, src/SafetyInputs.cpp, include/WebServer.h, src/WebServerRoof.cpp, src/main.cpp
tools/demo-core/ emulated_roof.*, emulated_device*.{h,cpp}, bridge.cpp
test/test_inputs_report, test/test_config_setup (new); test/test_config_migration
web/src: components/Wizard.tsx, components/wizard/* (new), components/SetupPrompt.tsx (new),
  hooks/useConfigEditor.ts (new), hooks/useRoofInputs.ts (new), components/Settings.tsx,
  settings/{DeviceTab,TimeTab,RoofTab,AlertsTab}.tsx (exports), System.tsx, Layout.tsx, App.tsx,
  types, validation/configSchema.ts, lib/docsLinks.ts, demo (device, handlers, panel), mocks, i18n
web/tests/wizard.spec.ts (new), screenshots.spec.ts
tools/api/{routes,operations}.json, tools/api/examples, tools/contracts/generate_schemas.py,
  specs/001-platform-bootstrap/contracts/schemas/{config,roof-inputs}.schema.json, docs/api/openapi.json
docs/user-guide/setup-wizard.md (new), mkdocs.yml, docs/getting-started/first-setup.md,
  docs/hardware/commissioning.md, docs/user-guide/{configuration,dashboard}.md, docs/api/rest.md,
  docs/live-demo.md, CHANGELOG.md, specs/ROADMAP.md
```

**Structure Decision**: no new motion or command code; one small report type beside the verdict
it reports, and the wizard as one more page of the app that reuses Settings' editor and cards.

## Phases

1. **Pure logic, tests first**: setup settings and migration; the inputs report writer.
2. **Firmware glue**: load migration, report in `SafetyInputs`, route; zero warnings; flash.
3. **Demo core**: the report from the simulation; demo defaults `done`; WASM rebuilt.
4. **Web**: shared editor, wizard pages, Test, review, prompt and banner, System card, demo panel;
   Vitest and Playwright; contracts, OpenAPI.
5. **Docs**.
6. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
