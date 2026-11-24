# Implementation Plan: Setup Wizard Journey

**Branch**: `spec/025-wizard-journey` | **Date**: 2026-11-23 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/025-wizard-journey/spec.md`

## Summary

Rework the setup wizard (web UI only) into one installer's journey of twelve parts, keeping
`setup.step` (0 to 11) as the resume point. The motor part hosts spec 019's jog and teach and spec
016's calibration run by reusing the Calibrate page's components; the alert step becomes a
multiple choice with each channel's fields (and MQTT's broker) inline; SQMeter's address is asked
once; screens that only linked out, asked for page-only answers or repeated SQMeter's answers are
removed. The resume and step-state bugs are fixed at their root (a screen that changed a setting
when it opened; an explicit start that never let go; Skip for now not recording progress; answers
kept only in the page). No firmware, setting, route or safety rule changes.

## Technical Context

**Language/Version**: TypeScript 5 (strict), Preact 10

**Primary Dependencies**: preact-router, zod (unchanged); the existing hooks `useJog`, `useTeach`,
`useCalibration`, `useConfigEditor`, `usePins*`, `useAlertsView`

**Storage**: the device's settings through `POST /api/config` (unchanged), `setup.step` as before

**Testing**: Vitest + Testing Library against the MSW mocks; Playwright against the demo (which
runs the device core, including the simulated roof's jog, teach and calibration run)

**Target Platform**: the device's web UI (LittleFS) and the demo

**Project Type**: web UI of an embedded device

**Performance Goals**: none new; the motor screens poll as the Calibrate page does (teach 2/s,
run 1/s)

**Constraints**: SIZE-02 (UI at 74% of its 75% LittleFS limit: the App chunk has about 2.6 KB of
gzipped room before it takes another 4 KB block), SIZE-01; functions at most 60 lines, complexity
15; DS rules; i18n for every string

**Scale/Scope**: about 12 source files in `web/src/components`, their tests, the user guide

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | How this plan satisfies it |
|---|---|
| I. Motion Safety | No motion code changes. The wizard hosts the same attended jog (hold-to-run, keep-alives, device deadline), teach and calibration run as the Calibrate page, with the same checks and the device re-checking at start. Nothing moves without a person holding a button or starting a run. |
| II. Close Only Onto a Parked Mount | The park check (or the test box's "no telescope" bypass) gates every jog and run as on the Calibrate page; the motor screens name a bypass in effect. |
| III. Unsafe Ends Closed | The weather check gates jog and run; the test box branch keeps the weather bypass rules (never with an SQMeter source; the SQMeter screen won't switch the source on while it is bypassed). |
| IV. Alpaca | Unchanged. |
| V. Testable Pure Logic | Screen order, visibility and resume are pure functions in `wizard/screens.ts` with Vitest tests; the journey is walked end to end in Playwright on both branches. The password bug fix has a test that fails without it. |
| VI. Budgets | Firmware untouched. The UI removes more than it adds; `size_check.py` is run. |
| VII. Quiet, Consistent UI | Built from the onboarding set (Modal with the Stop bar, ChoiceGroup, SpeechBubble) and Settings controls; DS-12 gains the checkbox form of the choice card group (one documented variant, no new style). Every note is a `Note`. |
| VIII. Trusted-LAN Security | The password question stays, now near the end; secrets stay masked; no new endpoint. |
| IX. Docs | `docs/user-guide/setup-wizard.md` rewritten; calibration-run, test-box, alerts and the coding standard's DS-12 updated; screenshots regenerated. |
| X. Code Quality | Quality gate run; no suppressions added. |
| XI. Heritage | No copied module changes. |

Result: pass. No complexity exceptions.

## Project Structure

### Documentation (this feature)

```text
specs/025-wizard-journey/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/wizard-journey.md
├── checklists/requirements.md
└── tasks.md
```

### Source Code (repository root)

```text
web/src/components/
├── Wizard.tsx                 # resume, Skip records the part, save-and-leave, fixes in place, field-naming errors
├── Calibrate.tsx              # StartForm, Running, Result, Checks exported; plain (card-less) form
├── calibrate/TeachCard.tsx    # JogControls exported; plain form
├── ui.tsx                     # ChoiceGroup gains the checkbox form (DS-12)
├── settings/controls.tsx      # SettingsCard plain form
├── settings/AlertChannelsCard.tsx  # channel groups without their own switch
├── settings/NetworkTab.tsx    # MqttFields exported
├── pins/WiringCheck.tsx       # no Calibrate link inside the wizard
└── wizard/
    ├── pages.ts               # the twelve parts
    ├── screens.ts             # the screens, their parts and visibility
    ├── context.ts             # the screens' props (page-only answers removed)
    ├── DevicePages.tsx        # name, SQMeter, site and time, password
    ├── BoardPages.tsx         # network, board (Custom inline), wiring (+ current sense)
    ├── HardwarePages.tsx      # test box, park switches, manual motor values
    ├── MotorPages.tsx         # direction, teach, calibration (new; reuses the above)
    ├── SourcePages.tsx        # N.I.N.A. with Alpaca, alerts (multiple), daylight
    ├── TestPanel.tsx          # N.I.N.A.'s Test only
    ├── review.ts, ReviewPage.tsx
    └── __tests__/
web/src/i18n/en.json, en.context.json
web/tests/wizard.spec.ts, wizard-a11y.spec.ts, screenshots.spec.ts
docs/user-guide/setup-wizard.md (+ calibration-run, test-box, alerts), docs/development/coding-standards.md
```

**Structure Decision**: the existing web UI layout; one new wizard file for the motor screens.

## Complexity Tracking

None.
