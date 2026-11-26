# A Person's Stop May Lapse (Owner Setting)

**Branch**: `spec/044-stop-hold-setting` | **Amends**: spec 030 (a person's stop of the automatic close),
spec 026 (`closeRearmSeconds`) | **Constitution**: 10.1.0 -> 11.0.0, III (MAJOR: relaxes a person's stop)

## Owner decision

When a person presses Stop during the automatic close (unsafe weather), the close stands down until
the roof is closed, an Open starts it moving, a person releases it, or the weather has been safe for
`autoClose.rearmMinutes`. It never lapses while unsafe, and the critical "Automatic close stopped"
alert is repeated every `autoClose.remindMinutes`.

That stays the default: "you'd stop a close because you see it would collide, so you go fix it; it
must not restart by itself". It becomes a setting, so an owner who worries more about a Stop
forgotten in the rain can let it lapse.

## Requirements

- **FR-001** `autoClose.personStopLapse`: `never` (default; also for settings saved before this
  spec), `rain` or `always`. Any other value is refused on save, never read as a default; one stored
  by a later release loads as `never` (the store is never lost).
- **FR-002** `autoClose.stopHoldMinutes`: 2-60, default 5, a whole number; outside the range is
  refused. The close's policy clamps it again (`STOP_HOLD_RANGE`).
- **FR-003** `never`: the stand-down behaves exactly as before (spec 030, spec 026).
- **FR-004** `rain`: a person's stand-down (`HeldByPerson`) also ends once it has rained for
  `stopHoldMinutes`. Rain is the verdict's reasons holding rain or a failed rain sensor
  (`skipsHold()`, fix 042's `HOLD_SKIP_REASONS`, any source, even a stale one), with the weather not
  safe. Other unsafe weather (cloud, wind, a lost or stale source) keeps holding.
- **FR-005** `always`: it ends once the weather (the sequence's view after the weather hold and a
  person's open override, daylight left out) has been unsafe for `stopHoldMinutes`.
- **FR-006** The time is added up from the first step after the stop, at most 1 s per step, and
  forgotten after a break (no rain, or safe) as long as `stopHoldMinutes`, as the weather hold adds up.
  A new Stop (a person's, or a remote one counted as a person's) starts it over.
- **FR-007** A lapse: the sequence goes to `Idle` with the event `holdLapsed`; the next step starts an
  attempt (park request, the park confirmed, then the close), through every gate the close has, so a
  later spec's gates (spec 043: a park request only with the roof at an end) apply too.
- **FR-008** A lapse is logged (warning) and sends the critical alert "Hold lapsed: closing because
  it's still raining" (or "... still unsafe") as the `roof_not_closed` event: past the pause, never
  below Urgent, queued until a channel takes it, not dropped when the sequence goes idle.
- **FR-009** The stop's own alert says when it lapses ("After 5 minutes of rain the hold lapses and the
  close goes ahead by itself; press Stop again to hold it longer.") when a lapse is set.
- **FR-010** A halted close (`Halted`), a stop while the close can't move, and the stop of a close that
  only daylight wants with the weather safe never lapse.
- **FR-011** Status: while the lapse counts, `roof.closeRearmSeconds` is the time left to it (it was
  `null` while unsafe) and the new `roof.closeRearmLapse` is `true`; otherwise `closeRearmLapse` is
  `false` and `closeRearmSeconds` is as before. Home Assistant's **Hold lapses in** reads the same
  seconds. The MQTT state document is unchanged (its buffer is nearly full).
- **FR-012** The web UI: Settings → Roof → Automatic close has **A person's Stop lapses** (Never, In
  rain, In any unsafe weather) and, unless Never, **Lapse after** (min). The dashboard's held line says
  "The close goes ahead by itself in m:ss if it keeps raining" while it counts.
- **FR-013** A restart keeps the stand-down (spec 010) but not the lapse count: it starts again, so a
  restart only ever delays a lapse.

## Risk (constitution 11.0.0, III)

With a lapse set, the roof can start closing minutes after a person stopped it, while they may still
be at the roof. Mitigations: off by default and after an upgrade; only the owner sets it; at least
2 minutes; only while it still rains (or is unsafe); a new Stop restarts the count; the dashboard,
status and Home Assistant count it down; the stop's alert says when it lapses and the lapse raises a
critical alert; the close still needs the park confirmed.

## Tests (`test/test_close_stop_lapse`, web `moveBlockers` and `RoofTabHold`)

- `never`: six hours of rain hold, only the reminders; the safe spell still re-arms; the alert text
  is unchanged.
- `rain`: lapses at exactly 5 min of rain, then a park request; doesn't lapse for other unsafe weather
  (six hours); rain and a failed rain sensor count, nothing else does; short breaks add up, a long one
  forgets.
- `always`: lapses after 5 min unsafe, says rain when it rains; safe counts the re-arm spell instead.
- A new Stop starts it over; the close after a lapse waits for the park; a halted close and a daylight
  stop never lapse; a closed roof ends the hold first; a late step counts 1 s.
- Config in and out, defaults for old settings, bad values refused; the policy and its clamp; the
  alert texts; the status fields.
