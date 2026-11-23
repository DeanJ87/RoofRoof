# Tasks: Park Source and SQMeter Secrets

**Input**: spec.md, plan.md, research.md, data-model.md, contracts/secrets-and-park-source.md

## Phase 1: Setup

- [x] T001 Constitution 5.0.0 (Principle II, Sync Impact Report with the new risk) in `.specify/memory/constitution.md`; roadmap entry and design decision in `specs/ROADMAP.md`

## Phase 2: Park source (US1-US4) - tests first

- [x] T002 [P] Native tests `test/test_park_source`: settings (default, upgrade from each `switches`, legacy save, strict combinations, test box with switches only), the park rule with switches only (one/two, open, unreadable, none fitted, slew veto fresh/stale, N.I.N.A. failures ignored), the hold (no ride, no bound), the test box bypass never with switches only, the switch watch (count, clear, boundary at 3, saturation, unfitted), the Open gate refusal and its text
- [x] T003 `ParkConfig::Source` with JSON in/out, upgrade rule and validation in `lib/ConfigModel`; `park.source` a motion setting in `lib/RoofIoLogic/src/SettingsGuard.cpp`
- [x] T004 `ParkInputs::switchesOnly`, `ParkNoSwitchFitted`, slew veto in `lib/SafetyInputLogic/src/ParkRule.cpp`; `closeHold` in `ParkHold.cpp`; `TestBox::Sources::switchesOnly`
- [x] T005 `lib/SafetyInputLogic/include/SwitchWatch.h`, `src/SwitchWatch.cpp`
- [x] T006 `RoofIo::GateInputs::switchCheck`, `GateRefusal::SwitchCheck` (Open only), text; status `parkSwitches.source`, `.openings`, `.check` in `lib/RoofIoLogic`
- [x] T007 Close sequence: no park request without an address (`ClosePolicy`); texts
- [x] T008 [P] Settings-deps entries for `park.source` (catalogue, `web/src/lib/settingsDeps.ts`, fixtures)

## Phase 3: Secrets (US5-US6) - tests first

- [x] T009 [P] Native tests `test/test_sqmeter_secrets`: the secrets parse (200, "" values, 401 with/without login, both 403s, other 403, 404, no reply, unreadable), the copy document (switches, secrets part, same/received/none, http), the placeholder fill (match, server changed, expired/none held, empty held, restore path refuses), the sync (secrets filled for the matching server only, never cleared, switches on only, Pushover on only with keys, held while roof commands on), the due rule, the sync status
- [x] T010 `lib/SafetyInputLogic` `SqmeterSecrets.h/.cpp`; `SiteConfig` switches and secrets; filter and document
- [x] T011 `CopiedSecrets` and the fill in `lib/ConfigModel` (`Config::applyJson`'s held secrets)
- [x] T012 Sync: switches and secrets in `SqmeterSync.cpp`; status `sync.secrets`

## Phase 4: Firmware glue and demo core

- [x] T013 `SafetyInputs`: source into the park inputs and the test box
- [x] T014 `RoofControl`: switch watch (count, clear, gate, NVS load/save beside the fault)
- [x] T015 `SafetyPoller`: 403 bodies parsed; secrets read after a good settings read; `SiteSync` pairs them
- [x] T016 `WebServerSite`: secrets in the copy (saved address only), held 30 min, same flags under the save lock; `WebServerApi`: held secrets into the save
- [x] T017 Demo core: source, watch, copy and sync secrets, the simulated SQMeter's `/api/secrets` (sharing injector)

## Phase 5: Web UI

- [ ] T018 Types, zod schema (`park.source`, the placeholder accepted in secret fields), mocks, `configPatch` pair
- [ ] T019 Settings park fields: source select and count, one-switch warning, N.I.N.A. optional note
- [ ] T020 Wizard: the park question (three choice cards) and the switch count follow-up
- [ ] T021 Copy: new rows, the secrets note and problems, the received placeholder in password fields; sync note
- [ ] T022 Dashboard/roof: Open's switch-check reason; status types
- [ ] T023 i18n and context notes; Vitest; Playwright demo flow

## Phase 6: Docs and contracts

- [x] T024 Docs: safety inputs (park source, guards), wiring (NO wiring, two switches), commissioning, configuration, REST, wizard, SQMeter site page; diagrams re-confirmed
- [ ] T025 OpenAPI, routes/operations (unchanged routes, new fields), contract schemas, api examples
- [x] T026 CHANGELOG

## Phase 7: Verify

- [ ] T027 Spec 001 quickstart §1: firmware build, native tests, web typecheck/test/build, Playwright, quality gate, flash budget, docs build; demo core rebuilt; source hash
