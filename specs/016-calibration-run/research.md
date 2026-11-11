# Research: Calibration Run

## R1. A leg is a controller move, not a second motion loop

**Decision**: `Roof::Controller::calibrate(leg, origin)` starts a move like `command(Open/Close)`
does, flagged as a calibration leg. It runs through the same `tick()`: the dead time, the late
step, the Hall rail, the leave time, the timeout, the drive budget, supply, current and over-current,
the latch, the estimate. Only four things differ, each decided in `lib/RoofLogic`:

- the timeout and the drive budget are the leg's `limitMs` (not the travel and speed settings,
  which are what the run measures);
- the duty follows the leg's profile (fixed, or a ramp up to the leg's duty and back to the slow
  duty after `slowAfterMs`), not the position-based profile;
- the end is the first confirmed magnet after leaving the start magnet, not the magnet the
  polarity setting names (or, with `expect`, that level only; the other is `wrongMagnet`);
- a leg is never "already there": it always drives.

The leg also integrates the duty it applied (`dutyMs`) and reports how it ended
(`calibrationReport()`).

**Rationale**: constitution I asks for one output writer and the motion rules in one place. A
separate calibration loop would duplicate the dead time and every fault. A leg toward the closed end
reports `closing`, so the park interlock (`interlockLost`) and the open gate's stop
(`openMustStop`) apply to it unchanged.

**Alternatives**: driving normal Open/Close moves with temporary settings (the timeout would come
from unmeasured settings, the profile depends on the estimate, and a wrong polarity faults before
the run can measure it); a separate driver in the run (two writers).

## R2. Telling the ends apart

**Decision**: the person says where the roof is at the start (closed, open, part-way), checked
against the Hall level. The run drives toward the closed end first and calls the level it stops
on the closed level; the slow open's level is the open level. The polarity setting is never used
to decide where the roof is during the run (the controller still labels its idle state by the
saved settings, as it always does).

**Rationale**: a wrong polarity is the most common first-run error; the run must measure it, so
it can't rely on it. A magnet seen at the start can't say which end it is; the person can see.
A wrong answer at an end drives into the end stop and faults `didNotLeave` within the leave time:
the same bound as a motor wired backwards on a first move today.

## R3. Bounds of each leg

**Decision**: `roof.calibrationLimitSeconds` (180 s default, 30-600 s) for the first close and the
slow open; the slow close: min(setting, 1.5 × slow open + 5 s); the top legs: min(setting, 1.25 ×
the slow leg in the same direction + 5 s). The drive budget during a leg is its limit, counted on
the same per-end total. Blockers at a leg's start: wait up to 15 s, then fail.

**Rationale**: the default roof's slow travel is 1850 mm ÷ (38 mm/s × 40 %) ≈ 122 s; 180 s covers
it with half again. Once a slow travel is measured, the next legs needn't be allowed more than a
generous multiple of it, so a magnet that comes loose mid-run costs seconds, not minutes, on the
end stop. Using the old budget would refuse a slower roof (exactly the one that needs
calibrating); the leg's own limit keeps the "stops and restarts don't reset it" property.

## R4. The top-duty legs

**Decision**: ramp from the slow duty to the top duty over `rampUpMs`, cruise, and from 60 % of the
time the slow leg predicts at the top duty (slow time × slow ÷ top) ramp back to the slow duty over
`rampDownMs`; stop at the magnet. Only from a confirmed magnet.

**Rationale**: a move above the slow duty from an unknown position is forbidden (constitution I);
a run starting part-way has reached the closed magnet before any top leg. The 60 % point arrives
at the slow duty unless the roof is more than 1.6 times faster at the top duty than the slow leg
suggests, which no DC worm drive is.

## R5. Speed and margin

**Decision**: each measured leg's speed at 100 % duty is travel ÷ its duty-weighted time (∫ duty
fraction dt), the controller's own model (speed proportional to duty). The proposal is the mean of
the top legs when run, else of the slow legs, rounded to 0.1 mm/s. The margin is the larger of 5 s,
20 % of a full move at the proposed speed, and 1.5 × the largest shortfall (a measured leg's time
less the time the proposed speed predicts for it) + 3 s, rounded up to a second.

**Rationale**: the top legs are the profile real moves use most. A DC motor's speed is closer to
affine than proportional in the duty, so the slow legs run longer than the top-duty speed
predicts; the shortfall term makes the margin cover them, so a move from an unknown position
(slow duty) still fits its timeout. 20 % of a move covers normal variation (temperature, wind,
supply).

**Withheld** when the open and close legs of the chosen pair (or of the slow pair) differ by more
than 30 %, or a measured leg took under 3 s: something slipped or a magnet was read early.

## R6. Hall statistics

**Decision**: the in-between reading from the middle half (by driven time) of every measured leg,
collected in 64 time buckets whose width doubles as a leg grows (a leg's length isn't known until
it ends); the reading at rest at each end from the 1 s pause after each arrival (and at the start
when the roof is at an end). Noise is the standard deviation.

**Rationale**: the readings near each magnet rise toward it and aren't noise. 64 buckets of
count, sum, sum of squares, min, max are about 1.5 KB and constant per step.

## R7. Thresholds

**Decision**: per level, the threshold stays where it is if it is at least the clearance (the larger
of 100 counts and 4 × the noise) beyond the in-between band and below the end's lowest reading at
rest by the rest margin (the larger of 10 counts and 3 × that end's rest noise); otherwise it is
clamped into that window. No window (or the ranges, 100-3995, 80 apart, can't be met): withheld.
Same side of the in-between mean, or less than 300 counts from it: withheld with the reason.

**Rationale**: the controller stops each leg where the current threshold is crossed, so the end
reading at rest is just past the current threshold, not the magnet's peak. Halfway between the
in-between level and that reading would move the stopping point inward on every run. Keeping a
threshold that works, and moving it only as far as needed, never creeps, and never moves a stopping
point past where the roof stopped during the run (so it can't introduce a missed magnet).

## R8. Other commands, the automatic close and platform actions

**Decision**: `CommandContext::calibrating` refuses open and close for every origin but the
automatic close; any accepted stop ends the run; `RoofControl::automaticClose` ends the run
before deciding (a closing leg is then "already closing"); `PlatformState::calibrating` holds
everything a move holds; `RoofControl::moving()` is true for the whole run.

**Rationale**: an attended run must not be taken over by an imaging app or an MQTT command, but
constitution III's close must always be able to happen; holding flash writes for the whole run
keeps the measured settings fixed and the control task's timing clean.

## R9. Accepting a proposal

**Decision**: the web UI saves the proposed values with `POST /api/config` (the Settings editor's
save, zod first, then the device's validation and `PlatformGuard::FlashWrite`). The device keeps
the run's report in RAM until the next run or a restart, and has no apply route.

**Rationale**: one save path, one set of rules (spec 015 R3); nothing can change a setting without
a person's save.

## R10. The status document

**Decision**: `GET /api/roof/calibration`, open like `/api/status`, written from a copy of the run's
report taken under the roof's lock (about 0.5 KB), outside it. `roof.calibrating` in the status
document drives the Stop bar.

**Rationale**: the run's page polls once a second; the status socket would grow every message
for a page that is rarely open.
