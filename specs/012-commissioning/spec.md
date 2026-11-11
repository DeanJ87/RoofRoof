# Feature Specification: Commissioning Guide

**Feature Branch**: `spec/012-commissioning`

**Created**: 2026-11-11

**Status**: Draft

## Context

Specs 002 to 011 built the roof controller. Each one left a "Bench check (pending)" list of what
only the real board or roof can confirm, and each recorded assumptions the owner has not yet
reviewed. This spec turns them into one ordered procedure and a printable checklist, finishes the
README and extends troubleshooting. It changes no firmware and no web UI.

## Clarifications

### Session 2026-11-11

- Q: In what order do the stages run? -> A: Safety order: the motor stays disconnected through the
  weather and park sources, the alert channel, the driver and fault tests and the close-on-unsafe dry
  run; the EN pull-down is measured before any firmware runs; the roof is not left unattended before an
  alert has reached the owner's phone and the dry run has passed on the real roof.
- Q: What stands in for the roof while the motor is disconnected? -> A: A magnet held at the Hall
  sensor by hand (the firmware's bench mode, once available, may replace it; the steps say so).
- Q: Which bench items are not owner commissioning? -> A: Those that need a logic analyser or a debug
  build (500 ms task block, 200-round soak, captured N.I.N.A. replies, stack low-water). They are
  listed as optional advanced checks, with a pointer to the native test that covers each.
- Q: How are the roof's commands given in the guide? -> A: With short shell functions over the REST
  API, so the dashboard's buttons can replace them in one place.

## User Scenarios & Testing

### User Story 1 - Commission a new roof from the docs alone (Priority: P1)

The owner has built the roof and flashed the board. Following one page in order, they reach a roof
that moves within its limits, closes itself onto a parked mount and alerts them, without a step that
needs the source code.

**Independent Test**: A reader who has never seen the project can follow the guide against the demo or
a bare board up to the stage that needs the roof; every command, setting name and log line in it
exists in the merged code and docs.

**Acceptance Scenarios**:

1. **Given** the guide, **When** read in order, **Then** the motor is disconnected for every stage
   until the dry run has passed, and the EN pull-down is measured before the first power-up.
2. **Given** the guide, **When** the unattended gate is read, **Then** it needs an alert received on a
   phone and the close-on-unsafe dry run on the real roof.
3. **Given** every bench-check item of PRs #3 to #11, **When** looked up in
   [bench-trace.md](checklists/bench-trace.md), **Then** it maps to a step of the guide or is listed as
   an advanced check with the reason.

### User Story 2 - A printable checklist (Priority: P1)

The owner prints one page, ticks boxes and writes down the measured values (Hall counts, travel,
speed, thresholds) that are settings.

**Acceptance Scenarios**: every guide step has a box with the same number; the settings measured are
listed in a table to fill in; the page prints without the site navigation.

### User Story 3 - Troubleshooting and README (Priority: P2)

A symptom met while commissioning (wrong direction, `didNotLeave`, `timeout`, `hallRail`, refusals,
no alert) has a row in troubleshooting with the cause and the page that explains it. The README's status
and links match what the device does.

## Requirements

- **FR-001**: One ordered guide, `docs/hardware/commissioning.md`, in the safety order of the clarification.
- **FR-002**: Every "Bench check (pending)" item of the merged specs appears as a guide step or an
  advanced check (FR-005), traced in `checklists/bench-trace.md`.
- **FR-003**: Every owner assumption that affects commissioning is placed in the step that exposes it
  (listed in `bench-trace.md`).
- **FR-004**: A printable checklist, `docs/hardware/commissioning-checklist.md`, with the guide's step
  numbers, and print styling in the docs stylesheet only.
- **FR-005**: Advanced checks (analyser, debug build) are separated and not part of the unattended gate.
- **FR-006**: Troubleshooting gains a commissioning section; stale "later spec" wording in the docs is
  corrected to what is merged.
- **FR-007**: The README's status and links are current.
- **FR-008**: Diagrams carry their metadata; `mkdocs build --strict` and `tools/docs/diagrams.py` pass.
- **FR-009**: No firmware or web UI change.

## Constitution rules relied on

| Principle | Rule | Where in the guide |
|---|---|---|
| I | First moves attended; a person at the roof with the supply cut within reach | Safety rules, stage 8 |
| I | An unknown position or a fault: only a person moves or clears | Stage 2 step 4, stage 6 step 6, stage 8 step 9, stage 9 step 6 |
| II | Close only onto a confirmed park | Stage 4, stage 6 step 1, stage 7 |
| III | Unsafe ends closed; no remote stop of an automatic close | Stage 7, stage 9 step 5 |
| VIII | Password protection before commands | Stage 2 step 3 |
| IX | Docs in the same change, diagrams with metadata | FR-008 |

## Success Criteria

- **SC-001**: Every item of the bench lists maps to a guide step or an advanced check (bench-trace.md).
- **SC-002**: `mkdocs build --strict` passes and `tools/docs/diagrams.py` reports no stale or incomplete diagram.
- **SC-003**: Every setting, route, button and log line the guide names exists in the merged code (checked after the last rebase; recorded in the PR).
- **SC-004**: An independent review of the procedure found no step out of safety order (recorded in the PR).

## Assumptions decided for the owner

Each was an open choice; the safest option the constitution allows was taken.

1. **The motor stays disconnected until the close-on-unsafe dry run has passed**, which makes the real
   roof's first move later than some owners would like. A hand-held magnet stands in for the roof.
2. **Password protection is turned on before any roof command** (stage 2), although it is off by default.
3. **First moves at the slow duty**: `roof.topDutyPercent` is set to 40 for the first open and close and
   restored after the speed is measured.
4. **No deliberate jam or stall.** The timeout and over-current stops are tested by shortening the
   margin or lowering the current limit, with a person at the supply switch, not by blocking the roof.
5. **A low supply is tested by raising `roof.minSupplyVolts`** above the supply, not by lowering the supply.
6. **The first unattended spell is short**, with alerts on, and watched from the phone.
7. **Advanced checks are not a gate**: they need an analyser or a debug build, and each rule they
   exercise has a native test.
8. **The commands are REST over curl** until the dashboard's roof card exists in the merged UI; the
   guide names the card's buttons where they replace them.

## Out of scope

Firmware, web UI, new settings, and anything that needs the real roof to be fabricated (no result is
claimed; the checklist is for the owner to fill in).
