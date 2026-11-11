# Contract: `lib/RoofLogic`

The library's public API, for the specs that build on it: 003 (hardware glue), 005 (close on
unsafe), 006 (Alpaca Dome), 008 (MQTT/HA), 011 (demo). Types are in [data-model.md](../data-model.md).

## Headers

| Header | Provides |
|---|---|
| `RoofTypes.h` | enums, `Fault`, `CommandResult`, `Readings`, `Output`, `Status`; `stateName`, `endName`, `faultName`, `refusalName`, `hallLevelName` |
| `RoofSettings.h` | `Settings`, `validSettings`, `settingsFromConfig`, the fixed values |
| `HallFilter.h` | `HallFilter` (classification with hysteresis and confirmation) |
| `MotionProfile.h` | `dutyFraction`, `advanceMm`, `expectedMoveMs`, `moveTimeoutMs`, `unknownMoveTimeoutMs`, `toDuty` |
| `RoofController.h` | `Controller` |

## `Roof::Controller`

```cpp
explicit Controller(const Settings &settings = Settings());

// One control step. Call at a fixed rate (spec 003: every 20 ms or faster; a gap over
// MAX_STEP_GAP_MS while driving is a lateStep fault). Returns the output to apply now.
Output tick(const Readings &readings);

// Open, close, stop or abort, between steps. Stop and abort always succeed and switch the output
// off at once; open and close may be refused (CommandResult::reason, measured, limit). Commands
// carry no time: the dead time, timeouts and estimate use only tick() times, so a command from
// another task with a skewed clock can't shorten the dead time. Abort with no move under way
// changes nothing (a stopped roof keeps its estimate: Alpaca AbortSlew on a stopped roof).
CommandResult command(Command command, Origin origin);

// A person clears the latched fault; Origin::Automatic is refused (notAPerson).
CommandResult clearFault(Origin origin);

// Latches `fault` and switches the output off at once. Spec 003 calls it at start-up with the
// fault it saved (a restart never clears a fault) and from its watchdog; spec 005 calls it when the
// park interlock drops during a close (Principle II).
void raiseFault(const Fault &fault);

// New settings, taken only while no move is active (else refused: Refusal::Moving). Invalid
// settings are taken but refuse every move (settingsInvalid) until valid ones arrive. Taking new
// settings while stopped part-way makes the position unknown (the estimate used the old ones).
CommandResult applySettings(const Settings &settings);

const Status &status() const;
Output output() const;
```

## Guarantees (each proven by a native test)

1. `output()` is Off (EN low, both PWM 0) unless a move is being driven. It is never a direction
   other than the one the move needs, and it is Off for at least `DEAD_TIME_MS` of step time
   between any two driven periods and after construction, counted from the first step after the
   driver went off.
2. `output().duty ≤ toDuty(topDutyPercent)`, and `≤ toDuty(slowDutyPercent)` while
   `status().fromUnknown`.
3. While `status().fault.kind != None`, `output()` is Off and every open and close is refused with
   `faultLatched`. Only `clearFault(Origin::Person)` clears it.
4. With the position unknown, open and close from `Origin::Automatic` are refused with
   `positionUnknown`.
5. A move is driven for at most its `moveTimeoutMs` (plus one step) and ends with the driver off on
   the step its target magnet is confirmed.
6. Every fault switches the output off on the step it is detected and makes the position unknown.
7. Three rail readings among the last eight latch `hallRail` on that step, consecutive or not.
8. The time driven toward each end since a magnet was last confirmed (or a person cleared a fault)
   never reaches `driveBudgetMs(settings)`; reaching it latches `driveBudget`. One reversal
   part-way never trips it. New settings at rest can shrink the budget below what is already used:
   the next drive then faults on its first step.
9. A step clock that goes backwards keeps the driver off (the dead time reads as not yet over).
10. No length of idle time changes how a move behaves: after weeks at rest a move drives after at
    most the normal pause, and its timeout, leave time and confirm windows count from that drive.

## What the caller must do (spec 003)

- Apply `output()` through `Driver::outputsFor` and `MotorDriver` after every `tick` and `command`.
- Save `status().fault` when it changes, and `raiseFault` it at start-up before the first step.
- Feed NaN for a supply or current reading it can't take; never substitute a default.
- Refuse settings saves that change `roof.*` while `status().state` is opening or closing, and hand
  new settings to `applySettings` once the move ends.
- Word faults and refusals for people from the kind, measured value and limit (constitution III).
