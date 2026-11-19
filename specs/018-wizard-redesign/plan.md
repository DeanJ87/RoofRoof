# Implementation Plan: Setup Wizard Redesign

**Branch**: `spec/018-wizard-redesign` | **Date**: 2026-11-11 | **Spec**: [spec.md](spec.md)

## Summary

- `web/src/components/ui.tsx`: the onboarding set: `Modal` (focus trap, Escape, focus restore,
  required Stop bar slot), `ChoiceGroup` (native radios as cards), `SpeechBubble`, `Mascot`, and
  `ProgressMeter`'s `valueText`; a few more `Icon` paths (back, close, lock, bell, sun, plug).
- `web/src/index.css`: tokens (`--scrim`, `--mascot-*`, `--radius-dialog`), `color-scheme: dark`,
  the modal, choice, bubble and mascot styles, the blur with its fallback, reduced motion. The
  spec 015 page-list and heading styles go.
- `web/src/components/wizard/screens.ts(x)`: the screen list (FR-007) with each screen's page,
  question, why, condition, answered rule and body; the bodies reuse the Settings fields
  (`DeviceNameField`, `PasswordFields`, `LocationFields`, the time zone and NTP fields, the
  current-sense fields, `HallCard`'s fields, `roofNum`, `WeatherSourceFields`' URL and topic,
  `NinaUrlField`, the alert channel groups, `DaylightModeFields`' threshold) and spec 015's
  `TestPanel`, `SupplyCheck`, `ReviewPage` checks.
- `web/src/components/Wizard.tsx`: the dialog: screen index, selection rule, Continue / Skip for
  now / Back with spec 015's save, step and discard rules, the leave screen, the done screen.
- `web/src/App.tsx`, `Layout.tsx`: `/wizard` renders the dashboard with the wizard as a modal; the
  layout makes the rest inert and leaves its Stop bar to the modal.
- Settings: small refactors so the field groups can be used without their cards (`TimeZoneFields`,
  `NtpFields`, `CurrentFields`, `HallFields`, exported channel groups). Settings looks the same.
- i18n: the questions, whys, choice titles and details, button labels, with context notes.
- Tests: Vitest for the components (focus trap, Escape, restore, radio group, progress text) and
  the wizard (every spec 015 guarantee on the new screens); Playwright for the flow, Stop over the
  dialog, keyboard, and axe on every screen × {320, 1280} × {light, dark}; screenshots.
- Docs: the setup wizard guide, first boot, the coding standard's DS rules (new DS-11 to DS-13 and
  DS-29), the constitution amendment (2.1.0) with its sync report, CHANGELOG, roadmap.

## Technical Context

| | |
|---|---|
| **Language/Version** | TypeScript strict, Preact 10.29 |
| **Primary Dependencies** | spec 015's wizard, `useConfigEditor`, `TestPanel`, `review.ts`; spec 009's `RoofStopBar` |
| **Storage** | none new |
| **Testing** | Vitest + Testing Library + MSW; Playwright + axe-core against the demo build |
| **Target Platform** | the device's web UI and the demo |
| **Project Type** | web UI + docs |
| **Constraints** | SIZE-01 (≤ 10 KB growth; target ≤ 6 KB), SIZE-02, WCAG 2.2 AA, phone width 320 px |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | No motion path added; every save is spec 015's save through `POST /api/config`, which the device refuses while moving or a close waits. | Vitest (refused save keeps the screen), Playwright (save refused while moving) |
| II. Parked mount | Untouched. | - |
| III. Unsafe ends closed | Untouched; the wizard reads the verdict only. | - |
| IV. Alpaca | Untouched (the Alpaca switch is a setting, as in 015). | - |
| V. Pure logic | The screen list and selection rule are pure functions with Vitest tests. | Vitest |
| VI. Budgets | UI growth measured and recorded; no firmware change. | `size_check.py` |
| VII. UI | Amended (MINOR) to admit the onboarding set; Stop inside every modal; reasons for an unavailable Continue; i18n; a11y; phone width. | Vitest, Playwright + axe, `copy_check.py`, `label_check.py` |
| VIII. Security | Save path, `requireAuth` and masked secrets unchanged. | existing |
| IX. Docs | Guide, first boot, DS rules, constitution, screenshots. | `mkdocs build --strict`, `ui_paths.py` |
| X. Quality | check.py, function limits. | check.py |
| XI. Heritage | `ui.tsx` is a copied module: the additions are recorded in `heritage.md` (diverged, not an upstream candidate unless SQMeter gains a wizard). | - |

**Gate**: pass after the VII amendment, which this PR makes through the Governance section.

## Project Structure

```text
specs/018-wizard-redesign/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
.specify/memory/constitution.md (VII, 2.1.0, sync report)
web/src/components/ui.tsx, index.css
web/src/components/Wizard.tsx, components/wizard/{screens.tsx (new), ChoiceScreens.tsx (new), pages.ts, context.ts,
  DevicePages.tsx, HardwarePages.tsx, SourcePages.tsx, ReviewPage.tsx, TestPanel.tsx}
web/src/components/settings/{TimeTab,RoofTab,AlertChannelsCard}.tsx (field groups exported)
web/src/App.tsx, components/Layout.tsx
web/src/i18n/{en.json,en.context.json}
web/src/components/__tests__/{Wizard.test.tsx, Onboarding.test.tsx (new)}
web/tests/{wizard.spec.ts, wizard-a11y.spec.ts (new), screenshots.spec.ts, a11y/inventory.ts}
docs/user-guide/setup-wizard.md, docs/getting-started/first-setup.md, docs/development/coding-standards.md,
  docs/development/heritage.md, docs/assets/screenshots/wizard-*.png, CHANGELOG.md, specs/ROADMAP.md
tools/ui/size-baseline.json
```

## Complexity Tracking

None. The VII change is an amendment, not an exception.
