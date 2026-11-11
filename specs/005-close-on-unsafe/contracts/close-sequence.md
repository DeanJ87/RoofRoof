# Contract: Close Sequence

What this spec offers the specs that build on it (006 Alpaca, 007 roof alerts, 008 MQTT and Home
Assistant, 009 dashboard, 011 demo). Types are in [data-model.md](../data-model.md).

## Pure API: `lib/CloseSequenceLogic` (`CloseSequence.h`)

```cpp
namespace SQM::CloseSeq {
class Sequence {
public:
    void setParkTimeoutMs(uint32_t ms);      // park.timeoutSeconds * 1000; applies from the next attempt
    Decision step(const Inputs &in);         // one step; at most one action and one event
    Decision closeAnswered(CloseAnswer answer); // the roof's answer to the Close action just carried out
    Phase phase() const;
};
void applySafety(Inputs &in, Safety::Verdict verdict, const Safety::ParkResult &park); // only Safe is safe
void applyRoof(Inputs &in, const Roof::Status &status, bool closePending);
bool isCritical(Event event);
std::string reasonFor(const Decision &decision, const Reasons &reasons); // which text explains the event
AlertText alertFor(const Decision &decision, const std::string &why);    // {title, message}
const char *phaseName(Phase); const char *eventName(Event); const char *causeName(Cause);
}
```

## Firmware: `SQM::CloseOnUnsafe` (`include/CloseOnUnsafe.h`)

```cpp
void begin(const Config &config);           // setup()
void updateConfig(const Config &config);    // any task: park.timeoutSeconds
void handle(SafetyInputs &safety, RoofControl &roof); // main loop, every pass after handleSafety()
void setAlertSink(std::function<void(const Alerts::Alert &)> sink); // main loop; spec 007 replaces the wording
```

`RoofControl` gains `Outcome automaticClose(uint32_t personStopsSeen)`: the close with the automatic
origin, refused (`personStop`) if a person's stop was counted since the snapshot showing
`personStopsSeen`, checked under the same lock as the command.

`RoofIo::Snapshot` gains `personStops`: a person's stops and aborts since start, counted under the
roof's lock before the stop is applied, so no snapshot shows the stop without the count (it isn't
written to the status document).

`WebServer` gains `void raiseRoofAlert(const Alerts::Alert &alert)` (main loop): dispatches to every
channel that is switched on, past the pause, the start-up grace and the master switch.

## Alert type

`Alerts::AlertType::Roof`, event name `roof`, level `wake`. It appears in `/api/alerts/recent`, the
MQTT alert topic and the webhook's `event`. Spec 007 adds per-event types and settings and may
retire it.

## Settings

| Key | Type | Default | Range | Meaning |
|---|---|---|---|---|
| `park.timeoutSeconds` | integer | 240 | 30-1800 | How long the sequence waits for the park to be confirmed before alerting |

## Guarantees (each proven by a native test)

1. The sequence never produces an open.
2. A close is produced only when the verdict is not safe, the roof is neither closed, unknown nor in
   error, the sequence isn't stood down, and the park has said parked for the last 4 s.
3. Two park requests are never less than 30 s apart.
4. After a person's stop (while running, waiting or unable to move), nothing is produced until the
   roof reads closed or the verdict has been safe for 10 minutes without a break; a close decided
   before a stop the roof has counted is refused.
5. Nothing is produced while the position is unknown or a fault is latched.
6. An attempt that doesn't see the park confirmed raises "mount not parked" exactly when
   `park.timeoutSeconds` has passed (at once without N.I.N.A.).
7. A close that faults, is refused, is interrupted or outlasts its guard raises "roof didn't close";
   the guard also produces a stop and halts the sequence; no close follows a refused or interrupted
   one within 5 minutes.
8. Each critical event alerts at most once every 30 minutes, and is raised again every 30 minutes
   while the roof stays open after it.
