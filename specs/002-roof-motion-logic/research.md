# Research: Roof State and Motion Logic

Each decision: what was chosen, why, and what else was considered.

## R1. One controller object, stepped with readings

- **Decision**: `Roof::Controller` holds the state. `tick(readings)` advances one control step and
  `command(cmd, origin)` takes a command between steps (no time: see R11); both leave the driver output ready in
  `output()`. Time is `uint32_t` milliseconds and every elapsed time is an unsigned difference, so
  the 49.7-day wrap is harmless.
- **Rationale**: Spec 003's motion task calls `tick` at a fixed rate and applies `output()` through
  `MotorDriver`; a command from any task is queued to that task and applied with `command`. One
  owner of the state, no globals (SMELL-15), no clock inside (STRUCT-01).
- **Alternatives**: a pure function `step(state, inputs) -> (state, output)` (cleaner in theory, but
  the state is ~20 fields and the function would need an options struct anyway; the object is the
  same thing with less copying); separate classes per state (more code for seven states).

## R2. The speed model and the position estimate

- **Decision**: speed is taken as proportional to the duty: `v = speedMmPerSecond × duty / 100%`.
  The estimate advances each step by `v × dt` using the duty applied during that step, clamped to
  `[0, travelMm]`.
- **Rationale**: a DC motor under PWM is close to linear above its stall duty, and the estimate only
  has to choose when to slow down; the magnet is the truth and the timeout catches the rest.
- **Alternatives**: a measured speed table per duty (needs commissioning data we don't have);
  constant speed whatever the duty (makes the slow phases wrong by 2.5×).

## R3. The profile: soft start by time, slow-down by estimated distance

- **Decision**: duty fraction `f = min(up(t), down(r))` with
  - `up(t) = s + (m − s) × min(1, t / rampUpMs)` (soft start from the slow duty `s` to the top duty `m`);
  - `down(r) = s + (m − s) × min(1, r / Z)` where `r` is the estimated distance left and
    `Z = speed × m × rampDownMs` (the distance covered at top duty during the ramp-down time);
  - `f = s` once the estimate reaches the end, until the magnet or the timeout.
  A move from an unknown position holds `f = s` throughout.
- **Rationale**: starting at the slow duty rather than 0 means the motor turns from the first step
  (below some duty a worm motor just hums), so the estimate is right from the start. Slowing by
  distance (not by a fixed time before the expected arrival) adapts to moves that start part-way.
  Since `f ≥ s > 0` the slow-down always reaches the end in finite time.
- **Alternatives**: a trapezoid in time (wrong for part-way starts); a distance setting for the slow
  zone (the roadmap asks for ramp times; the distance is derived instead).

## R4. Expected move time and the timeout

- **Decision**: `expectedMoveMs(settings, distanceMm)` integrates the same profile: the ramp-up
  numerically in 10 ms steps (at most 3000 steps, since ramp-up is at most 30 s), then cruise and
  the slow-down zone in closed form (`Z / (V(m−s)) × ln(f₀/s)`, or `r/(V m)` when `m = s`). The
  timeout is `expected + timeoutMarginSeconds`. A move from an unknown position gets
  `travelMm / (V s) + margin`. The clock starts when the driver turns on (after the dead time).
- **Rationale**: the timeout must follow the profile actually driven, otherwise slow ramps cause
  false timeouts; the closed form keeps the cost bounded on the ESP32 for any valid setting. The
  ranges (travel ≤ 20 m, speed ≥ 1 mm/s, slow duty ≥ 10%) keep every time below 2.1 × 10⁸ ms, well
  inside 32 bits.
- **Alternatives**: a fixed multiple of travel/speed (too loose for jams, too tight for slow
  settings); simulating the whole move per command (up to millions of steps for slow settings).

## R5. Hall classification

- **Decision**: raw ADC counts. `≥ highThreshold` is the magnet of the end set as high (setting),
  `≤ lowThreshold` is the other end's, `≤ 30` or `≥ 4065` is a rail (fault), anything else is no
  magnet. A confirmed magnet zone is left only when the reading passes its threshold by 40 counts
  (hysteresis). A zone becomes confirmed after 3 identical readings in a row; until the first
  confirmation the zone is "unsettled" and moves are refused.
- **Rationale**: SS49E output is 0.86-4.21 V at 5 V; through the board's divider it sits well
  inside the ADC's range, so a rail means a disconnected or shorted sensor. Three readings at 50 Hz
  is 60 ms (about 1 mm at the slow speed), enough to reject single-sample ADC noise without
  overrunning the magnet. The hysteresis stops an idle roof parked on a threshold from flickering.
- **Alternatives**: millivolts (would need the divider in the logic; 003 owns scaling and the
  calibration UI shows counts); a moving average in the logic (003 may average the ADC; doing both
  slows the stop).

## R6. Faults and their confirmation

- **Decision**: fault kinds `timeout`, `didNotLeave`, `wrongMagnet`, `hallRail`, `overCurrent`,
  `currentUnreadable`, `supplyLow`, `lateStep`, `driveBudget`, `external`. Over-current and supply low (or
  unreadable) must last 250 ms while driving; a late step is a gap > 100 ms between steps while
  driving; the others act on the step they are seen (Hall ones after the 3-reading confirmation).
  Each records the measured value and the limit: elapsed and timeout (ms), amps and limit, volts and
  minimum, raw counts and rail, gap and allowed gap.
- **Rationale**: the motor's start-up current and the supply's dip at start are brief; a quarter of
  a second ignores them but stops a stall long before it heats the motor. A late step means the
  motor ran for an unknown time unsupervised.
- **Alternatives**: blanking the current check for the first second of a move (would ignore a jam
  at the start, the most likely place for one).

## R7. Order of checks in a step

1. Late step (if driving). 2. Hall: rail → fault. 3. Latched fault → off, done. 4. Moving: wait out
the dead time, then: wrong magnet → fault; target magnet → arrive; didn't leave → fault; timeout →
fault; current, supply → fault; else the profile's duty. 5. Idle: follow the Hall zone.

- **Rationale**: a sensor fault or a wrong magnet makes everything after it meaningless; the target
  magnet ends the move before electrical checks because the driver is off either way and the roof is
  where it should be (spec Clarifications).

## R8. Fixed values vs settings

- **Decision**: settings are the roof's measured facts and preferences (roadmap list plus the open
  direction, the leave time and the supply minimum). Fixed: dead time 500 ms, confirmation 3
  readings, hysteresis 40 counts, rails 30/4065, confirm time 250 ms, allowed gap 100 ms.
- **Rationale**: the fixed values describe the ADC, the driver and the control loop, not the roof;
  the constitution's "never constants" covers travel, speed and the Hall thresholds, which are
  settings. Each fixed value is a named `constexpr` in `RoofSettings.h` with its reason.

## R9. States and their meaning for later specs

| Logic state | Meaning | Alpaca ShutterStatus (006) | HA cover (008) |
|---|---|---|---|
| `closed` | closed magnet read | Closed (1) | closed |
| `open` | open magnet read | Open (0) | open |
| `opening` | a move to open (including the dead time) | Opening (2) | opening |
| `closing` | a move to close | Closing (3) | closing |
| `stopped` | stopped part-way, position estimated | Open (0) - ASCOM has no part-open | open, with position |
| `unknown` | between the magnets with no estimate | Error (4) | unknown (null state) |
| `error` | fault latched | Error (4) | unknown |

## R10. Settings in the model

- **Decision**: one flat `roof` object in the config JSON (`roof.travelMm`, ...), enums as strings
  (`openDirection`: `forward`/`reverse`, `hallHighEnd`: `open`/`closed`). Ranges in
  `test/fixtures/config-ranges.json`; cross-field rules (slow ≤ top duty; high − low ≥ 80) checked
  in `ConfigValidate.cpp` and the zod schema with the same messages. All `roof.*` settings go in
  the dependency catalogue's `noDependency` list: each one's effect needs nothing outside its own
  section.
- **Rationale**: the shared range fixture addresses one level below a section, so a flat object
  keeps every range testable by both sides; flat keys also match how spec 009's settings tab binds
  fields.
- **Alternatives**: nested `roof.hall.*` / `roof.currentSense.*` (the range fixture and both of its
  test harnesses only address `section.key`).

## R11. Changes from the safety review

- **Commands carry no time.** First the dead time was measured from the `nowMs` a command passed
  in. A command stamped later than the next step made the unsigned difference wrap, and a
  stale stamp shortened the pause. Now the dead time runs from the first step after the driver went
  off, and `command` and `raiseFault` take no time at all. Construction counts as a stop, so the
  first drive after start-up waits too. *Alternative rejected*: validating stamps against the last
  step (keeps a second clock in the API for no benefit).
- **Rails in a window.** Consecutive-only confirmation let an intermittent connector (good, rail,
  good, rail...) avoid both the rail fault and the arrival, leaving only the hard timeout. Now
  rail readings are counted in a window (3 in the last 8) and kept out of the magnet's run.
  *Alternative rejected*: any single rail reading while driving faults (one ADC spike would stop
  the roof).
- **Drive budget.** Each move had its own timeout, so stop/restart cycles of a jammed roof with
  current sense off never timed out. The total driven time since a magnet (or a person's clear) is
  now capped at the longest single move. *Alternative rejected*: a budget counted in restarts (a
  person nudging the roof a few times is legitimate).
- **Reading sanity.** Supply readings above 16 V or not finite, and current readings below -0.5 A
  or not finite, are unreadable: a 12 V system can't read 16 V, and the IBT-2's sense outputs can't
  go negative beyond ADC offset noise.

### R11a. From the re-review

- **The budget is per direction.** A single budget false-tripped on one legitimate reversal near an
  end: the open leg and the close leg together can need nearly two travels of driving between
  magnets (reproduced with top 60 / slow 40, top 50 / slow 40, slow = top = 100, slow 80). That
  would have blocked the close on rain (constitution III). Counting each direction separately keeps
  the jam protection for restarts in one direction and bounds alternating restarts at twice the
  budget.
- **The dead time is compared signed.** An unsigned difference made a step clock that went
  backwards look like a long-finished pause; a negative difference now counts as still waiting.
