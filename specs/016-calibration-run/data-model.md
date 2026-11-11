# Data Model: Calibration Run

## Setting

| Path | Type | Default | Range | Meaning |
|---|---|---|---|---|
| `roof.calibrationLimitSeconds` | integer s | 180 | 30-600 | The longest a calibration leg may drive before it faults `timeout` (the first close and the slow open; later legs are bounded by the measured ones too) |

A roof setting, so a save that changes it is refused while the roof moves or a run is active.
Older stored settings load with the default.

## lib/RoofLogic

```cpp
// RoofSettings.h
constexpr uint32_t MIN_CALIBRATION_LEG_MS = 1000;
constexpr uint32_t MAX_CALIBRATION_LEG_MS = 600000;

// RoofTypes.h
struct CalibrationLeg {
    End target = End::Closed;                // drive toward this end (openDirection maps it)
    uint8_t dutyPercent = 0;                 // slow duty .. top duty
    uint32_t slowAfterMs = 0;                // above the slow duty: back to it after this much driving (0: never)
    uint32_t limitMs = 0;                    // hard timeout and drive budget, MIN..MAX_CALIBRATION_LEG_MS
    HallLevel expect = HallLevel::Unsettled; // Low/High: must end on it; Unsettled: any magnet
};
struct CalibrationReport {                   // the last calibration leg that ended
    uint32_t ended = 0;                      // count of ended legs (a new one bumps it)
    bool arrived = false;                    // at a magnet; false: stopped, replaced or faulted
    End target = End::Closed;
    HallLevel startLevel = HallLevel::Unsettled;
    HallLevel level = HallLevel::Unsettled;
    uint32_t drivenMs = 0;                   // driver on to stop
    float dutyMs = 0.0f;                     // ∫ duty fraction dt over drivenMs
};
Status::calibrating                          // the active move is a calibration leg
Refusal::LegInvalid ("legInvalid")           // a leg outside its ranges
```

`Controller::checkCalibrate(leg, origin) const` / `calibrate(leg, origin)`: refused for any origin
but `Person` (`notAPerson`), with a fault (`faultLatched`), during a move (`moving`), on the
readiness checks every move has, for an invalid leg (`legInvalid`), and above the slow duty
without a confirmed magnet (`positionUnknown`).

`MotionProfile.h`: `float legFraction(const Settings &, const CalibrationLeg &, uint32_t drivenMs)`.

## lib/CalibrationLogic

```cpp
namespace SQM::Calib {
enum class Where : uint8_t { Closed, Open, PartWay };            // "closed", "open", "partWay"
enum class LegKind : uint8_t { ToClosed, SlowOpen, SlowClose, TopOpen, TopClose };
enum class RunState : uint8_t { Idle, Running, Done, Failed, Stopped };
enum class Phase : uint8_t { Settle, Wait, Drive };              // while running
enum class Ending : uint8_t { None, Done, Stopped, Weather, Park, CloseSequence, Fault, Refused, Busy };

constexpr uint32_t SETTLE_MS = 1000;        // at each end before the next leg
constexpr uint32_t LEG_WAIT_MS = 15000;     // the longest a leg waits for blockers
constexpr uint16_t MIN_END_SEPARATION_COUNTS = 300;
constexpr uint16_t MIN_CLEARANCE_COUNTS = 100;
constexpr uint16_t MIN_REST_MARGIN_COUNTS = 10;
constexpr uint32_t MIN_MEASURED_LEG_MS = 3000;
constexpr float MAX_LEG_DISAGREEMENT = 0.30f;
constexpr float WARN_LEG_DISAGREEMENT = 0.15f;
constexpr float CURRENT_WARN_FRACTION = 0.8f;

struct Request { bool attended; Where at; uint32_t travelMm; bool topPass; };
struct StartInputs { uint32_t nowMs; uint32_t limitMs; bool sequenceIdle; bool closePending;
                     bool settingsPending; RoofIo::GateInputs gates; bool busy;
                     std::string busyReason, busyText, faultText; };
struct StepInputs { Roof::Readings readings; bool currentSense; RoofIo::GateInputs gates;
                    bool busy; std::string busyText, faultText; };
struct Answer { bool accepted; std::string reason; std::string text; };

struct Stats { uint32_t count; double sum, sumSq; uint16_t min, max; ... };
class LegSamples { 64 buckets, width doubles from 250 ms; middle(totalMs) };
struct Electrical { uint32_t count; double ampsSum; float ampsPeak; float supplyMin; };
struct MeasuredLeg { LegKind kind; End target; uint8_t dutyPercent; uint32_t drivenMs; float dutyMs;
                     Roof::HallLevel level; Electrical electrical; };
struct Measurements { MeasuredLeg legs[4]; uint8_t legCount; Stats between, closedRest, openRest;
                      Roof::HallLevel closedLevel, openLevel; uint32_t travelMm; bool currentSense;
                      float maxCurrentAmps; };
struct Problem { const char *reason; std::string text; };   // reason "" = none
struct Proposal { uint32_t travelMm; bool speed; float speedMmPerSecond; uint32_t timeoutMarginSeconds;
                  Problem speedProblem; bool hall; Roof::End hallHighEnd; uint16_t hallHighCounts,
                  hallLowCounts; Problem hallProblem; std::vector<Problem> warnings; };
Proposal propose(const Measurements &, const Roof::Settings &old);

struct Report { RunState state; Phase phase; Request request; uint32_t limitMs; uint8_t legIndex,
                legTotal; LegKind leg; uint32_t legStartMs, legLimitMs; Ending ending;
                std::string endingText; Measurements measured; Roof::Settings old; bool proposed;
                Proposal proposal; };

class Run {
  Answer start(Roof::Controller &, const Request &, const StartInputs &);
  bool wantsLeg(uint32_t nowMs) const;            // the glue probes its blockers only then
  void step(Roof::Controller &, const StepInputs &);
  void stopped(const std::string &who);           // any accepted stop or abort
  void yieldToClose();                            // the automatic close takes over
  bool active() const;
  const Report &report() const;
};
void writeDocument(JsonObject out, const Report &, uint32_t nowMs);
}
```

## lib/RoofIoLogic

- `CommandContext::calibrating`: open and close refused with reason `calibrating` and the text
  "A calibration run is under way: stop it first" for every origin but `Automatic`.
- `PlatformState::calibrating`: `platformHold` answers `Moving` for every action while it is set.
- `Snapshot::calibrating`, written as `roof.calibrating` in the status and MQTT state documents'
  roof object (status only).
- `settingsSaveRefused(state, motionChanged, calibrating)`.

## Firmware

`RoofControl`: `startCalibration(request, sequenceIdle)`, `stopCalibration()`,
`calibrationReport()` (a copy under the lock), the run stepped in `stepLocked` after the
controller's tick; `moving()` true while a run is active.
