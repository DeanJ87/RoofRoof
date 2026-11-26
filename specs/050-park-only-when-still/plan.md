# Plan: A Park Request Never Stops a Slew

**Spec**: [spec.md](spec.md) | **Constitution**: 12.1.0 (II, V, VI, IX)

## Approach

1. **The sender** (`lib/SafetyInputLogic/ParkSend.h`, `ParkSend.cpp`): `ParkSender` with
   `request(nowMs)` (false when one already waits), `step(ParkSendInputs)` returning one event
   (`None`, `Waiting`, `StopSlew`, `Send`, `SendAtLimit`, `AlreadyParked`, `Dropped`),
   `waitingForSlew()` for the status and `reset()` for a new address. At the 180 s limit it asks for
   the stop slew (finding M06), and decides the park on a reading taken after the stop; 30 s after
   the stop it sends the park anyway (`SendAtLimit`). It keeps N.I.N.A.'s last good slewing reading itself (two
   words), so the poller hands it only the latest reading.
2. **The poller** (`src/SafetyPoller.cpp`): a request (`parkRequested`) goes to the sender and makes
   N.I.N.A. due (`PollSchedule::ninaWanted`). After the pass's N.I.N.A. poll, while a request waits,
   the sender steps on the latest reading, `setMountMayMove` and whether an address is set; `Send`
   and `SendAtLimit` call `sendPark` as before; `StopSlew` calls it with `stopOnly` (the new
   `Nina::Api::stopSlewRequest`/`parseStopSlew`, `GET /v2/api/equipment/mount/slew/stop`, not
   recorded as a park) and makes N.I.N.A. due at once. Each event is logged once. The snapshot carries
   `parkWaitingForSlew`. The step (`stepPark`), `requestPark` and `snapshot` move to
   `src/SafetyPollerPark.cpp`, keeping `SafetyPoller.cpp` under the 600-line limit (LIMIT-04).
3. **The roof at the send**: `SafetyPoller::setMountMayMove` (an atomic, like `setRoofMoving`), set by
   the main loop every pass before `handleSafety` from `RoofControl::parkRequestAllowed`.
4. **The text**: `ParkResult::askWaiting`, set in `SafetyInputs::evaluate`; `describe` appends it.
5. **Tests, docs, changelog, demo core, constitution 12.1.0.**

## Budgets

No task, no buffer kept: the sender is a few dozen bytes inside the poller object (static storage).
While a request waits the pass copies one `MountReading` (its `detail` string, empty when good).

## Complexity Tracking

None: Principle II gains a rule (MINOR); nothing is relaxed.
