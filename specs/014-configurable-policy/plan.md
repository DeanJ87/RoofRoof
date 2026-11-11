# Implementation Plan: Configurable Policies

**Branch**: `spec/014-configurable-policy` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/CloseSequenceLogic`: a `CloseSeq::Policy` (the timings and choices of the automatic
  close, defaults equal to today's constants), `Sequence::setPolicy` which clamps every value,
  `policyFrom(Config)`; the not-started retry with its own cause; alert wording from the policy;
  `AlertRetry` capped by the reminder interval.
- `lib/RoofIoLogic`: `CommandContext::remoteStopsClose` and `CommandCheck::standsDown` (a
  remote stop honoured by the owner's setting counts as a person's); `StateSchedule` with a
  settable at-rest change gap.
- `lib/DaylightLogic`: the band from the settings.
- `lib/ConfigModel`, `lib/SettingsDeps`: the new settings, ranges, JSON, validation and the
  dependency entries.
- Firmware: `CloseOnUnsafe` takes the policy from the config (under its lock); `RoofControl`
  sets the remote stop rule; the MQTT state schedule takes the gap.
- Demo core: the same policy and remote stop rule.
- Web: an "Automatic close" card in Settings → Roof with an "Advanced" fold, the band in the
  Daylight card, the confirmation option on the Motion card and in the Roof card, the Alpaca note,
  schemas, types, mocks, defaults, i18n.
- Docs: configuration, close-on-unsafe, alpaca (the N.I.N.A. anchor), mqtt, dashboard,
  commissioning guide and checklist, settings dependencies, CHANGELOG, ROADMAP.
- `tools/close-diff`: a differential fuzz of the close sequence against `origin/main`.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the
settings contract in [contracts/settings.md](contracts/settings.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | specs 005 and 013's `CloseSeq::Sequence`; spec 006's `checkCommand`; spec 008's `StateSchedule`; spec 009's Roof tab and Roof card; spec 011's demo core |
| **Storage** | NVS: one new section (`autoClose`), one (`ui`), and two fields in existing sections |
| **Testing** | `pio test -e native` (new `test_close_policy`; ranges from `config-ranges.json`; every existing test unchanged); the differential fuzz; Vitest; Playwright |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) |
| **Constraints** | defaults identical to `main`; flash within SIZE-04; UI size limits; stored config under 5,100 bytes |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | No setting reaches `lib/RoofLogic`, the fault latch or the position rules; the sequence's only motion actions stay Close (through the gates) and Stop. | every `test_roof_*` unchanged; `test_close_policy`: `test_policy_never_closes_unparked` (random policy) |
| II. Parked mount | The park hold can only be raised from 4 s and the close still passes spec 003's park gate. | `test_close_policy`: `test_park_hold_limits`, property "never closes unparked" |
| III. Unsafe ends closed | Every timing bounded; stand-downs always alert; reminders never off; the remote stop counts as a person's (with the alert) only by the owner's setting; hotspot and committed-restart rules fixed. | `test_close_policy`: `test_remind_limits`, `test_rearm_limits`, `test_not_started_retry`, `test_remote_stop_*`; `tools/close-diff` (defaults identical) |
| IV. Alpaca | The simulator keeps the default context (remote stop refused); ConformU unchanged. | ConformU CI |
| V. Pure logic | Policy, clamping, retry, remote stop rule and the schedule gap in `lib/`. | `test_close_policy`, `test_mqtt_schedule`, `test_daylight` |
| VI. Budgets | A few fields; flash measured; stored size tested at the longest values. | build, `flash_budget.py`, `test_config_model` |
| VII. UI | Existing cards and controls; one fold component with a reason; Stop never asks. | Vitest, Playwright, `size_check.py`, `copy_check.py` |
| VIII. Security | Alpaca stays off by default; the hotspot rule stays fixed; settings save keeps `requireAuth`. | `test_config_model` defaults |
| IX. Docs | Configuration, close-on-unsafe, alpaca, mqtt, commissioning; diagrams re-checked. | `diagrams.py`, `mkdocs build --strict` |
| X. Quality | check.py, function limits. | check.py |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/014-configurable-policy/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
lib/CloseSequenceLogic/ include/{CloseSequence.h,ClosePolicy.h,AlertRetry.h} src/{CloseSequence.cpp,CloseSequenceDaylight.cpp,CloseSequenceText.cpp,ClosePolicy.cpp,AlertRetry.cpp}
lib/RoofIoLogic/ CommandCheck.h/.cpp, RoofMqtt.h/.cpp
lib/DaylightLogic/ Daylight.h/.cpp
lib/ConfigModel/, lib/SettingsDeps/
include/CloseOnUnsafe.h, src/CloseOnUnsafe.cpp, include/RoofControl.h, src/RoofControl.cpp, src/WebServerStatus.cpp (MQTT gap)
tools/demo-core/emulated_roof.*, tools/close-diff/ (new)
test/test_close_policy (new), test/fixtures/{config-ranges.json,settings-deps/*}
web/src: types, validation, settings (RoofTab, AutoCloseCard, controls Fold), RoofCard, mocks, i18n
docs/user-guide/{configuration,close-on-unsafe,alpaca,mqtt,dashboard}.md, docs/hardware/commissioning*.md, docs/reference/settings-dependencies.md, CHANGELOG.md, specs/ROADMAP.md
```

**Structure Decision**: one policy struct owned by the sequence, filled from the config by one
pure function shared by the firmware and the demo, so there is still one close path.

## Phases

1. **Pure logic, tests first**: policy and clamping; not-started retry; wording; alert retry cap;
   remote stop rule; schedule gap; band; config.
2. **Firmware glue**: policy hand-off, remote stop, MQTT gap; zero warnings; flash.
3. **Demo core and web**.
4. **Docs**.
5. **Verify**: differential fuzz against `origin/main`; quickstart §1 of spec 001; converge;
   independent reviews until one says safe to merge.

## Complexity Tracking

None.
