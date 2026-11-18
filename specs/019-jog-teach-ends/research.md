# Research: Jog and Teach the Ends

## R1. The jog as a controller move

- **Decision**: a jog is a spec 016 calibration leg with a `jog` flag: slow duty, no ramp, stop at
  the first magnet after leaving the start magnet, `expect` any level. Its timeout is
  `min(JOG_MAX_MS, unknownMoveTimeoutMs(settings))`; its budget is `driveBudgetMs(settings)`; the
  step stops it (a stop, not a fault) once `nowMs` reaches the hold deadline.
- **Rationale**: the leg already runs through the one step with the dead time, the leave time,
  every electrical check and the latch, tells the ends apart by direction (the polarity may be
  wrong) and is watched by the park interlock in both directions (`Status::calibrating`). Reusing
  it keeps the new motion code to a few lines.
- **Alternatives**: a normal person's move from an unknown position (would raise `wrongMagnet` on a
  roof whose polarity is wrong, and has no hold deadline); a separate jog state machine (duplicates
  every check).

## R2. Keep-alive and its bound

- **Decision**: `JOG_HOLD_MS` = 500 ms, renewals every 150 ms from the page, release = a person's
  Stop. The deadline is taken from the first step after the jog command or renewal (a renewal sets
  a flag; the step stamps it), never from the caller's clock.
- **Rationale**: Principle I: "a busy network can delay a command; it can never prolong a move".
  With the deadline in the step, a stalled network or browser can only end a jog early. 500 ms
  lets two keep-alives go missing (normal Wi-Fi jitter) without a stutter; at the default slow
  duty the roof moves about 8 mm after the last one. A shorter bound stutters on a phone; a longer
  one moves the roof further after a dropped link.
- **Alternatives**: a WebSocket heartbeat (the status socket is one-way, and a second socket costs
  heap); renewing from any command (a second browser could hold someone else's jog).

## R3. The jog's limits

- **Decision**: 30 s per jog; the normal drive budget across jogs.
- **Rationale**: `unknownMoveTimeoutMs` is Principle I's timeout for exactly this kind of move; a
  jog's is never longer, so the calibration run's exception isn't needed. 30 s stops a forgotten
  hold (a weight on a key, a stuck pointer) well before the motor heats on an end stop. The budget
  stops repeated jogs toward an end with no magnet seen.

## R4. Gates for both directions

- **Decision**: open gate (weather safe, no daylight close) and close gate (park holds) for both
  directions at the start; the interlock and `openMustStop` for both while it runs.
- **Rationale**: the motor direction is a setting that may be wrong on a roof being taught, exactly
  the uncertainty spec 016 handles the same way for its legs.

## R5. Reading window and marks

- **Decision**: a 50-reading ring of raw counts (one second at the 20 ms step), refilled from empty
  whenever a move or jog is active or the driver is on. A mark copies the ring's statistics
  (`Stats`: count, sum, sum of squares, min, max). Ready when the ring is full.
- **Rationale**: "the reading at rest averaged over a short window", O(1) per step, 100 bytes.

## R6. The proposal

- **Decision**: `fitHall(closed, open, partWay)` returns the high end and the two windows with the
  run's rules (`MIN_END_SEPARATION_COUNTS` 300, clearance `max(100, 4σ)`, rest margin `max(10, 3σ)`,
  clipped to 100-3995) or a `Problem` (`samePole`, `weakEnd`, `noRoom`). The run clamps its old
  threshold into each window; the teach takes the halfway point between the part-way mean and the
  end's mean, clamped into the window.
- **Rationale**: one rule for both; the halfway point is what the calibration page tells owners to
  do by hand, and leaves equal room either side. The windows lie at least the clearance outside the
  part-way readings, so the thresholds are at least 200 counts apart (the 80-count minimum gap and
  the 40-count hysteresis are met).

## R7. Confirm on the device

- **Decision**: the page sends the three values it shows; the device checks them against its
  proposal, writes them into a copy of the current config, validates it and saves it through
  `saveAsFlashWrite` (held while moving or under a platform action), then clears the marks.
- **Rationale**: the propose-and-confirm handshake the step asks for; only three values change, so
  another person's concurrent save of other settings is kept.

## R8. Position after a teach

- **Decision**: no new way to set the position. When the saved thresholds reach the controller
  (`applySettings` at rest), `followHall` makes the position known at the magnet the roof sits on.
- **Rationale**: Principle I: position is known only at a magnet. The person's confirmation is what
  makes the thresholds trustworthy; the magnet still has to be read.

## R9. Principle II wording

- **Decision**: PATCH 2.0.1. The rule's meaning since spec 004 (`park.switches` none, one or two;
  `ParkRule` with no switches confirms on N.I.N.A. alone) is unchanged; the text named GPIO32/33
  as if both were always fitted.
- **Rationale**: the governance section: PATCH is "wording and clarifications". The roadmap's design
  decision and the code both had the switches optional from the start.
