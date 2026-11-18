# Data Model: Jog and Teach the Ends

## lib/RoofLogic

| Name | Kind | Notes |
|---|---|---|
| `JOG_HOLD_MS` | `constexpr uint32_t` = 500 | a jog not renewed for this long (step time) stops |
| `JOG_KEEPALIVE_MS` | `constexpr uint32_t` = 150 | how often the page renews; reported to clients |
| `JOG_MAX_MS` | `constexpr uint32_t` = 30000 | a jog's hard timeout, if below an unknown-position move's |
| `Status::jogging` | `bool` | the active move is a jog (`calibrating` is true too) |
| `Refusal::NotAPerson` | existing | a jog from anyone but a person |
| `Controller::checkJog(End, Origin)` | `CommandResult` | person, no fault, not moving, readiness, valid settings |
| `Controller::jog(End, Origin)` | `CommandResult` | starts it; `jogId()` names it |
| `Controller::holdJog(uint32_t id)` | `bool` | renews the active jog with this id; false otherwise |
| `Controller::jogId()` | `uint32_t` | the id of the last jog started (1, 2, ...) |
| `Controller::jogLimitMs()` | `uint32_t` | `min(JOG_MAX_MS, unknownMoveTimeoutMs(settings))` |

Move fields: `jog`, `holdPending`, `holdUntilMs`. A jog is `calibration = true` with
`leg = {target, slowDutyPercent, 0, jogLimitMs(), Unsettled}` and `fromUnknown = true`.

## lib/CalibrationLogic

- `HallFit { Roof::End highEnd; double highLo, highHi, lowLo, lowHi; }` and
  `Problem fitHall(const Stats &closed, const Stats &open, const Stats &between, HallFit &out)`:
  the run's rule (research R6).
- `Answer attendedRefusal(const Roof::Controller &, bool attended, bool calibrating, const StartInputs &)`:
  the checks a run start and a jog start share: `notAttended`, `calibrating`, `closeSequence`,
  `faultLatched`, `moving`, `closeWaiting`, `settingsPending`, `weatherNotSafe`, `daylight`,
  `mountNotParked`, `busy`/`hotspot`/`restarting`.
- `REST_READINGS` = 50.
- `TeachProposal { bool made; bool ok; Roof::End highEnd; uint16_t highCounts, lowCounts; Problem problem; }`;
  `made` false until all three marks exist.
- `TeachProposal proposeTeach(const Stats &closed, const Stats &open, const Stats &partWay)`.
- `class Teach`:
  - `Answer jog(Roof::Controller &, Roof::End, bool attended, bool calibrating, const StartInputs &)`;
  - `Answer hold(Roof::Controller &, uint32_t id)`: `jogEnded` unless the controller renews it;
  - `void step(const Roof::Status &, uint16_t counts)`: the window;
  - `Answer mark(Where, const Roof::Status &, bool calibrating)`: `notAtRest`, `hallNotSettled`,
    `calibrating`;
  - `Answer checkConfirm(Roof::End, uint16_t high, uint16_t low) const`: `noProposal`,
    `proposalChanged`;
  - `void clear()`;
  - `TeachReport report(const Roof::Controller &, uint32_t nowMs) const`.
- `TeachReport`: jog (active, id, target, elapsedMs, limitMs), window (ready, mean), marks
  (`Stats[3]`, `bool marked[3]`), proposal.
- `void writeTeachDocument(JsonObject, const TeachReport &)`; `TEACH_DOCUMENT_CAPACITY`.

## lib/RoofIoLogic

- `CommandContext::jogging`; `JOGGING_REASON` = `"jogging"`.
- `openMustStop(status, gates)`: also when `status.jogging`.

## Status of a jog

```text
idle --jog()--> active (dead time, then driving at the slow duty)
active --holdJog(id)--> active (deadline moved)
active --deadline passed / Stop / Abort / weather or daylight turns--> stopped (position unknown unless at a magnet)
active --first magnet after leaving the start one--> arrived (position from the magnet)
active --timeout / budget / any fault / park lost--> fault latched
```
