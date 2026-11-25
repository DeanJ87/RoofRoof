# Feature Specification: The Roof Card Says Each Fact Once

**Feature Branch**: `spec/034-roof-card-one-fact`

**Created**: 2026-11-25

**Status**: Draft

## Context

With SQMeter out of reach, the dashboard's Roof card said the same thing four times, each time at
length:

- under the buttons, Open's blocker: "Open: Weather check: unsafe: SQMeter HTTP unreachable:
  connection refused (no good reply for 44.5 s > 30 s)", its "?", **Weather check settings**, and
  "Clears once SQMeter reports safe.";
- then "Close: Already closed" under a Close button that was already greyed, beside a **Closed** pill;
- under the Weather row, "No reply from SQMeter for 44 s: SQMeter HTTP unreachable: connection
  refused (no good reply for 44.5 s > 30 s)" and "Weather unsafe: closing in 46 s unless it clears";
- under the Daylight row, "SQMeter's answer can't be read: using the location saved Nov 25".

That breaks the design rules: each fact appears in one place (DS-08); text on the page is a one-line
note that reports a current state, explanations go behind "?" (DS-04); one fact per line, never run
on (DS-24); a note is one sentence of at most 90 characters (DS-22). Constitution VII asks for the
reason a button is unavailable through a "?" or a note, which a short line does.

Spec 026 made `lib/moveBlockers.ts` the one model of "why can't the roof move?" and spec 027 added
the move dialog that lists every reason when a blocked Open or Close is pressed. This spec keeps
both and makes the card the short form of the same model.

## User Scenarios & Testing

### User Story 1 - The card reads at a glance (P1)

A person opens the dashboard in the dark while SQMeter is silent. The card shows the buttons, one
short line saying why Open is blocked, one short line under Weather saying how long SQMeter has been
silent, and the hold's countdown. Pressing Open lists every reason in full.

**Acceptance**:

1. Given SQMeter silent and the roof closed, the card shows "Open: Weather unsafe" under the
   buttons, and nothing about Close (greyed, with the **Closed** pill).
2. The Weather row's note is "No reply from SQMeter for 44 s"; the device's reason ("SQMeter HTTP
   unreachable: connection refused ...") is behind the row's "?" and in full on Diagnostics.
3. With the roof open and the hold counting: "Closing in 46 s unless it clears", one line.
4. Pressing Open opens spec 027's dialog ("1 reason Open is blocked") with the full reason, when it
   clears and **Weather check settings**.

### User Story 2 - The same rule for every reason (P1)

1. Park: "Close: Mount not parked" (or once, unprefixed, when both buttons share it); N.I.N.A.'s
   reach is a short note under Mount park ("N.I.N.A. not reachable"), its reason behind "?".
2. Daylight: "Open: Sun is up"; with SQMeter's location unreadable, the Daylight row says "Using the
   location saved 25 Nov", why behind its "?".
3. A fault: the fault's own block (its text, "?", **Clear fault**) is the only place it is said; the
   greyed buttons carry it as a tooltip.
4. A stand-down: the **Automatic close** row's pill (Held, Halted) with what it means behind "?",
   then one line with when it lapses and **Release hold**.
5. Supply low, a Hall reading not settled, a calibration run, a waiting close, no live data, the
   device restarting, the setup hotspot and the stuck switch check: a few words each.

### Edge cases

- Several reasons block a button: the first (most important, the device's order of checks) is
  shown; the dialog's title counts them all.
- Both buttons share the line (no live data, restarting, the park, supply low): said once, without a
  button name, and both buttons point at it (`aria-describedby`).
- The device's text is already in the pill's "?" (the weather verdict usually names the source): it
  is not repeated there.
- Calibrate and the wizard keep the full blocker list (title, "?", clears, fix): they are where a
  person works through them.

## Requirements

- **FR-001**: Each blocker in `lib/moveBlockers.ts` MUST carry a `short` label of a few words
  (DS-20); the Roof card's line under the buttons MUST come from it, for the first blocker of each
  blocked button.
- **FR-002**: The Roof card MUST NOT show a line under a button greyed only because the roof is at
  that end, moving there, or held by a latched fault; the button's `title` MUST give the reason.
- **FR-003**: Any other reason a button is greyed MUST stay a visible line (constitution VII).
- **FR-004**: A line both buttons share MUST appear once, unprefixed, described-by from both.
- **FR-005**: SQMeter's and N.I.N.A.'s health notes MUST be short (no device text); the device's text
  MUST be behind the row's "?" (unless the pill's text already holds it) and in full on Diagnostics.
- **FR-006**: The weather hold MUST be one short line under the Weather row.
- **FR-007**: A stand-down under **Automatic close** MUST be one line (when it lapses) with
  **Release hold** while offered; what it is stays in the pill and its "?".
- **FR-008**: A kept location MUST be said apart from why SQMeter's could not be read: on the
  dashboard the why is behind the Daylight row's "?", in Settings it is a note of its own.
- **FR-009**: The move dialog (spec 027) MUST still list every reason in full.
- **FR-010**: The built UI MUST not grow (`tools/ui/size_check.py`, LittleFS at its 75% limit, the CSS
  file in its last 4 KB block): run-on strings are removed, no CSS is added.

## Success Criteria

- **SC-001**: With SQMeter silent and the roof closed, the Roof card shows three lines of status text
  (Open's line, the Weather note, the location line), down from eight.
- **SC-002**: The device UI's JS shrinks (152,215 to 152,147 bytes gzipped), the CSS is unchanged.
- **SC-003**: Vitest and Playwright pin the short lines, the tooltips, the "?" contents and the
  dialog's full reasons.

## Assumptions decided for the owner

- Only the first reason is on the card, without "and 1 more": the dialog's title counts them, and a
  count on the card cost bytes the budget doesn't have.
- At its end, moving there or held by a fault, a greyed button has no line: the **Closed**/**Open**/
  **Fault** pill and the fault block are on the same card, and the `title` tooltip (also the
  button's accessible description) repeats it. Constitution VII's "never without its reason" is met
  by those visible elements on the same card.
- The fault's tooltip is the state label "Fault" (the fault block below says what to do).
- Short labels reuse existing words where one exists ("Mount not parked", "Supply low",
  "Calibration run", "Fault"); new ones are "Weather unsafe", "Weather unknown" and "Sun is up".
- Settings → Roof → Location shows the location problem and the kept location as two notes (the old
  single line ran two facts together).
- Diagnostics keeps the long form ("No reply from SQMeter for 44 s: SQMeter HTTP unreachable: ...")
  as its value: it is the page for detail.
- Not changed: the Status card, Calibrate and the wizard's blocker lists, the move dialog, the
  nudge controls (spec 033), firmware.
