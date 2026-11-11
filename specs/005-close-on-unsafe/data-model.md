# Data Model: Close on Unsafe

All types are in `lib/CloseSequenceLogic/include/CloseSequence.h`, namespace `SQM::CloseSeq`.

## Inputs (one per step)

| Field | Type | From |
|---|---|---|
| `nowMs` | `uint64_t` | `SafetyPoller::nowMs()` (never wraps) |
| `weatherSafe` | `bool` | `SafetyInputs::weather().verdict == Safe` |
| `roof` | `Roof::State` | `RoofControl::snapshot().status.state` |
| `closePending` | `bool` | `snapshot().closePending` (spec 003's waiting close) |
| `moveTimeoutMs` | `uint32_t` | `snapshot().status.moveTimeoutMs` (0 when no move) |
| `parked` | `bool` | `SafetyInputs::park().parked` |
| `ninaConfigured` | `bool` | the park result has no `ParkNinaNotConfigured` reason |
| `personStops` | `uint32_t` | `snapshot().personStops`: a person's stops and aborts since start (counted before the stop is applied) |
| `roofStarted` | `bool` | `RoofControl::running()`: else the cause is `RoofNotStarted`, not `PositionUnknown` |

## Settings

| Field | Type | Default | From |
|---|---|---|---|
| `parkTimeoutMs` | `uint32_t` | 240 000 | `park.timeoutSeconds` × 1000 |

## Phase

| Phase | Meaning | Sends |
|---|---|---|
| `Idle` | safe, or the roof is closed | nothing |
| `WaitPark` | an attempt: waiting for the park to hold for 4 s | park requests (≥ 30 s apart) |
| `Closing` | the close was commanded (or a close was already running) | a stop, only on the guard |
| `RetryWait` | an attempt failed; 5 min before the next | close, if the park holds for 4 s |
| `HeldByPerson` | a person stopped the sequence | nothing |
| `CannotMove` | the position is unknown or a fault is latched | nothing |
| `Halted` | a close outlasted its guard and was stopped | nothing |

## Transitions

| From | Condition (checked in this order) | To | Action | Event (cause) |
|---|---|---|---|---|
| Idle | safe, or roof closed | Idle | - | - |
| Idle | roof error | CannotMove | - | CannotClose (FaultLatched) |
| Idle | roof unknown for 2 s | CannotMove | - | CannotClose (PositionUnknown) |
| Idle | roof unknown < 2 s | Idle | - | - |
| Idle | roof closing or close waiting | Closing | - | Started |
| Idle | an attempt ended by a safe verdict less than 10 min ago | the paused phase, its clock counting unsafe time only | as that phase decides | Started |
| Idle | otherwise | WaitPark | RequestPark if due | Started |
| WaitPark, RetryWait, Closing | roof closed | Idle | - | Closed |
| WaitPark, RetryWait, Closing | a person's stop since the last step | HeldByPerson | - | StoppedByPerson |
| WaitPark, RetryWait | roof error | CannotMove | - | CannotClose (FaultLatched) |
| Closing | roof error | CannotMove | - | CloseFailed (Fault) |
| WaitPark, RetryWait, Closing | roof unknown for 2 s | CannotMove | - | CannotClose (PositionUnknown) |
| WaitPark, RetryWait, Closing | roof unknown < 2 s | same | - | - |
| WaitPark, RetryWait | safe | Idle | - | EndedSafe |
| WaitPark, RetryWait | roof closing or close waiting | Closing | - | - |
| WaitPark, RetryWait | parked for 4 s | Closing | Close | - |
| WaitPark | N.I.N.A. not configured, or parkTimeoutMs of unsafe time since the attempt began, or 2 × parkTimeoutMs + 10 min of wall time | RetryWait | - | ParkNotConfirmed (NinaNotConfigured / ParkTimeout) |
| WaitPark | otherwise | WaitPark | RequestPark if due | - |
| RetryWait | 5 min of unsafe time since it began, or 15 min of wall time | WaitPark (new attempt) | RequestPark if due | Started |
| Closing | close waiting for 30 s | Halted | Stop | CloseFailed (TooLong) |
| Closing | closing for the move's timeout + 30 s (from the command, or from the start of a waiting close) | Halted | Stop | CloseFailed (TooLong) |
| Closing | not closing, not waiting, safe | Idle | - | EndedSafe |
| Closing | not closing, not waiting, unsafe | RetryWait | - | CloseFailed (Interrupted) |
| HeldByPerson, CannotMove, Halted | roof closed | Idle | - | Closed |
| HeldByPerson, CannotMove, Halted | safe for 10 min without a break | Idle | - | EndedSafe |
| CannotMove | a person's stop since the last step | HeldByPerson | - | StoppedByPerson |
| CannotMove | roof neither unknown nor error | RetryWait | - | - |

`ParkRefused` for the third time in a row in an attempt counts as `Refused` (`MAX_PARK_REFUSALS`).
The 10 minutes of safe that end a stand-down count from entering it. `restore(phase, event, cause)`
puts back a HeldByPerson or Halted kept across a restart, and its event is raised again on the
first step.

"Parked for 4 s" closes only when no close was refused or interrupted in the last 5 minutes; while
held back by that alone, WaitPark neither fails its attempt nor sends requests.
While in RetryWait, HeldByPerson, CannotMove or Halted with no other event, the episode's last
critical event is raised again (`reminder`) 30 minutes after it last alerted.

After a `Close` action the glue reports the roof's answer with `closeAnswered`:

| Answer | Meaning | To | Event (cause) |
|---|---|---|---|
| `Started` | accepted (moving, or waiting for an upload or MQTT connect) | Closing | - |
| `ParkRefused` | spec 003's gate: the park no longer holds | the phase the close came from (same attempt) | - |
| `Withdrawn` | a person's stop was counted after the snapshot the decision used (`RoofControl::automaticClose`) | the phase the close came from; the next step stands down | - |
| `Refused` | the motion logic refused (fault, supply, settings, not started) | RetryWait | CloseFailed (Refused) |

"RequestPark if due": only when no request was ever sent or the last was ≥ 30 s ago.

## Decision (per step)

| Field | Type | Meaning |
|---|---|---|
| `action` | `Action` | `None`, `RequestPark`, `Close`, `Stop`. There is no open. |
| `event` | `Event` | `None`, `Started`, `EndedSafe`, `Closed`, `ParkNotConfirmed`, `CloseFailed`, `CannotClose`, `StoppedByPerson` |
| `cause` | `Cause` | `None`, `ParkTimeout`, `NinaNotConfigured`, `Fault`, `Refused`, `Interrupted`, `TooLong`, `PositionUnknown`, `FaultLatched` |
| `alert` | `bool` | a critical event whose event and cause haven't alerted for 30 minutes (alert it); others are logged only |
| `limitMs` | `uint64_t` | the time limit that ran out (ParkTimeout, TooLong) |
| `reminder` | `bool` | the event is the episode's last critical event raised again |

Critical events: `ParkNotConfirmed`, `CloseFailed`, `CannotClose`, `StoppedByPerson`. An episode runs
from leaving `Idle` to returning to it; reminders stop when it ends.

## Alert

`CloseSeq::alertFor(decision, why)` gives `{title, message}` for a critical event, with the time
limit that ran out (`Decision::limitMs`); `why` comes from `reasonFor`: the park result's text,
the fault text, the refusal text, or the roof's state (constitution III: the value and the limit,
or the input that failed).

## Kept across a restart

`Sequence::saved()` / `restore(saved)`: the phase (WaitPark, RetryWait, HeldByPerson, Halted), the
episode's last critical event and cause, the time the phase has run (unsafe and wall), the
attempt's park limit, and how long ago the last park request went out. The glue keeps it in RTC
memory with a versioned magic and a checksum, every step; Idle clears it (except the request
spacing, kept while it matters). On restore, a WaitPark or RetryWait gets 10 s more
(`RESTART_ALLOWANCE_MS`) for the restart itself; times too long for the new time base are clamped
(the glue adds a week to its time base).

## Alert delivery

`AlertRetry`: after a critical alert is handed to the dispatcher, the dispatcher's view of its
record (`Pending`, `Delivered`, `Failed`, `NoChannel`) is checked every second; a `Failed` one is
sent again `ALERT_RETRY_MS` (60 s) after the last send. `Delivered`, `NoChannel` or an idle
sequence ends it.
