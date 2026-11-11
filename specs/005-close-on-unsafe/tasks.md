# Tasks: Close on Unsafe

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Required by constitution V for every `lib/` rule; written before the logic (TDD).

**Organisation**: US1-US6 are all the one pure sequence, so its unit tests are grouped by story and
the logic is implemented once; the settings, firmware and docs phases follow. Every task ends with
`pio test -e native` green; firmware tasks also with `pio run -e esp32dev`.

## Phase 1: Setup

- [x] T001 Add `lib/CloseSequenceLogic` (library.json, include/, src/) and its `-I` path to `[env:native]` in `platformio.ini`

## Phase 2: Tests of the sequence (write first)

- [x] T002 [US1] `test/test_close_sequence/test_main.cpp` (US1-US3) and `test/test_close_standdown/test_main.cpp` (US4-US6) and `test/test_close_text/test_main.cpp` (wording, mapping), sharing `test/close_rig/CloseRig.h`: start on unsafe and on unknown verdict; nothing when closed or safe; park request at start; close only after 4 s of parked (3999 / 4000 ms); a running close watched without a request; closed ends the sequence
- [x] T003 [US2] Same file: park timeout at limit-1 / limit; N.I.N.A. not configured fails at once; 5-minute back-off with no requests (299 999 / 300 000 ms); closing during the back-off when parked; park confirmed then lost restarts the 4 s; gate refusal back to waiting within the same limit; request spacing 29 999 / 30 000 ms; the timeout setting
- [x] T004 [US3] Same file: fault while closing; motion refusal; interrupted close; waiting close guard 30 s; moving close guard (move timeout + 30 s) with a stop; repeated alerts suppressed within an episode
- [x] T005 [US4] Same file: a person's stop in each phase stands down; no actions while stood down; 10 min safe re-arms (9:59 / 10:00); a broken safe run doesn't; a person's close while stood down ends it; stops before the episode ignored
- [x] T006 [US5] Same file: unknown for 1999 / 2000 ms; fault latched; resume after the position is known (back-off, close on park)
- [x] T007 [US6] Same file: safe during the park wait ends it; safe during the close lets it finish; flapping verdict keeps requests ≥ 30 s apart
- [x] T008 Same file: alert wording names the event and carries the reason text; names of phases, events and causes
- [x] T009 `test/test_close_properties/test_main.cpp`: ≥ 5,000 seeded random sequences of verdict, park, roof state, closePending, person stops and step gaps against a small roof model; invariants P1-P7 (no open; close only with 4 s parked, unsafe, roof movable, not stood down; requests ≥ 30 s apart; nothing while stood down; nothing while unknown or faulted; "mount not parked" raised by the limit; each critical alert at most once per episode)

## Phase 3: The sequence

- [x] T010 `lib/CloseSequenceLogic/include/CloseSequence.h` and `src/CloseSequence.cpp` until T002-T007 and T009 pass
- [x] T011 `src/CloseSequenceText.cpp`: names and `alertFor` until T008 passes
- [x] T012 Deliberate-breakage run (outside the repo): at least 12 breakages, each must make a test fail; record the result in the PR

## Phase 4: Settings (FR-016)

- [x] T013 `park.timeoutSeconds` in `Config.h`, `ConfigModel.cpp` (default), `ConfigJsonIn.cpp`, `ConfigJsonOut.cpp`, `ConfigValidate.cpp`; `test/test_config_safety` (default, round trip, boundaries, old config) and `test/fixtures/config-ranges.json`
- [x] T014 Web: `configSchema.ts`, `types/config.ts`, `mocks/config.ts`; settings-deps default-config fixture; contract schemas and OpenAPI regenerated

## Phase 5: Firmware

- [x] T015 `RoofIo::Snapshot::personStops`, counted by `RoofControl` under its lock before a person's stop or abort is applied
- [x] T016 `Alerts::AlertType::Roof` (`roof`), its ntfy tag, `test_alert_clients` name check; `WebServer::raiseRoofAlert`
- [x] T017 `include/CloseOnUnsafe.h`, `src/CloseOnUnsafe.cpp`: inputs, actions, `closeAnswered`, logging, alerts; `main.cpp` wiring (begin, updateConfig, handle after `handleSafety`)
- [x] T018 Build with zero warnings; flash budget

## Phase 6: Docs

- [x] T019 `docs/user-guide/close-on-unsafe.md` with the close-sequence diagram (DIA-22, blocking) and "Diagram in words"; `mkdocs.yml` nav
- [x] T020 `safety-inputs.md` (the sequence now exists; disagreement), `alerts.md` (the `roof` event), `configuration.md` (`park.timeoutSeconds`), `docs/api/rest.md` event names, heritage (`AlertEngine.h`), CHANGELOG

## Phase 7: Verify

- [x] T021 Demo core rebuild; spec 001 quickstart §1 (firmware, native, web typecheck/test/build, Playwright, quality gate, docs build, flash)
- [ ] T022 `/speckit-converge` until converged; independent safety review until it reports safe to merge
