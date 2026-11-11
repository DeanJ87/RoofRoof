# Tasks: Configurable Policies

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/settings.md](contracts/settings.md)

Tests first for every `lib/` change (constitution V). `[P]`: can run in parallel with the other
`[P]` tasks of its phase.

## Phase 1: Setup

- [x] T001 `tools/close-diff/` driver and `run.sh` (R8): builds a trace driver against
  `origin/main`'s `lib/` and this tree's, compares the traces; run once on the unchanged tree.

## Phase 2: Foundational (pure logic)

- [x] T002 `test/test_close_policy`: defaults equal the old constants; `clamp` at each limit;
  `policyFrom(default config)` equals `Policy{}`.
- [x] T003 `lib/CloseSequenceLogic/include/ClosePolicy.h`, `src/ClosePolicy.cpp`: `Policy`,
  `clamp`, `policyFrom`.
- [x] T004 `CloseSequence.h/.cpp`, `CloseSequenceDaylight.cpp`: every constant use through the
  policy; `setPolicy`; `Decision::retryMs`, `rearmMs`.
- [x] T005 `Cause::NotStarted` and the retry path; tests (US2 scenario 2).
- [x] T006 `CloseSequenceText.cpp`: wording from the decision's times; the NotStarted text.
- [x] T007 `AlertRetry::setMaxGap`; tests.
- [x] T008 [P] `CommandCheck`: `remoteStopsClose`, `standsDown`; tests (US3).
- [x] T009 [P] `RoofMqtt` `StateSchedule::setChangeMinMs`; tests.
- [x] T010 [P] `Daylight::Settings::bandDeg`; tests.
- [x] T011 `lib/ConfigModel`: the settings, JSON in and out, validation; `config-ranges.json`.
- [x] T012 `lib/SettingsDeps` and `catalogue.json`: D-45, D-44 extended, `noDependency`;
  fixtures.
- [x] T013 Random-policy property test (SC-004) in `test_close_policy`.

## Phase 3: Firmware and demo glue

- [x] T014 `CloseOnUnsafe`: policy under its lock; `AlertRetry` cap.
- [x] T015 `RoofControl`: remote stop rule from the config; count `standsDown`.
- [x] T016 MQTT state schedule gap from the config.
- [x] T017 Demo core: policy, remote stop rule; `bash tools/demo-core/build.sh`.

## Phase 4: Web UI (US2, US4, US5)

- [x] T018 Types, zod schemas, defaults, mocks, settings-deps (web), dep labels.
- [x] T019 `Fold` control; `AutoCloseCard.tsx`; Daylight band; Motion card option; Alpaca note.
- [x] T020 Roof card inline confirmation (Stop never asks).
- [x] T021 i18n strings and context notes; reworded hints that named "10 minutes".
- [x] T022 Vitest tests for the card, the fold, the note and the confirmation.

## Phase 5: Docs

- [x] T023 configuration, close-on-unsafe, alpaca (anchor "N.I.N.A. opens and closes the roof"),
  mqtt, dashboard, settings dependencies, commissioning guide and checklist.
- [x] T024 CHANGELOG, ROADMAP entry, diagrams re-checked, OpenAPI and contract schemas.

## Phase 6: Verify

- [x] T025 Differential fuzz (20,000 runs) against `origin/main`.
- [x] T026 Spec 001 quickstart §1; flash budget; source hash.
- [x] T027 Converge; independent review rounds until safe to merge.
