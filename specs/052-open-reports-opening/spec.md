# Feature Specification: An Accepted Open Reports Opening

**Feature Branch**: `spec/052-open-reports-opening` | **Created**: 2026-11-27 | **Constitution**: 12.2.0 -> 12.3.0 (III, IV)

## Context

**Finding F58 (the owner's first real session, 2026-11-26 18:26-18:29, N.I.N.A.'s log).** N.I.N.A.'s
Open Dome Shutter calls `OpenShutter`, waits about 1 s and reads `ShutterStatus` once; if it reads
closed it logs "ShutterStatus is still reported as closed after calling OpenShutter" and the step
fails. It did whenever RoofRoof accepted an open that then didn't happen. When the roof really
started, `ShutterStatus` read opening and N.I.N.A. waited correctly (18:33:25 -> 18:34:26).

**Finding F39/F39b (the same session).** After SQMeter's brownout restart the weather hold (spec 030)
had fired; the weather was safe again but the hold stays fired until the weather has been safe for
its length. An Open (N.I.N.A.'s and the owner's) passed every check, the roof started, and the armed
automatic close sent it back to closed within 0.25-0.29 s. N.I.N.A.'s read a second later: Closed.

What the code already did (spec 027, on the board then): a move waiting for the park reads Opening or
Closing with `Slewing` true; a failed wait reads Error until the next command; a refusal is an Alpaca
InvalidOperation with the reason. That path was never tested through the Alpaca router.

ASCOM (IDomeV3): `OpenShutter`/`CloseShutter` are asynchronous; they return once the operation has
started, with `ShutterStatus` opening/closing until it ends (open/closed), or error if it fails.
`Slewing` is true while any part of the dome moves or a move has been asked for and hasn't started.

## User Scenarios & Testing

### User Story 1 - Open after a weather blip (P1)

SQMeter blinks unsafe long enough to fire the hold, then reads safe. 20 s later N.I.N.A.'s sequence
opens the roof. `OpenShutter` returns InvalidOperation: "The automatic close is still armed after
unsafe weather and would close the roof again at once: an imaging app can't open it until the weather
has been safe for 60 s (40 s left)". Nothing moves; N.I.N.A. shows the reason. After 40 s the same
open runs.

### User Story 2 - Open queued behind the park (P1)

The mount is unparked; N.I.N.A. opens. `OpenShutter` succeeds; the next `ShutterStatus` (and every
one until the open end) reads Opening, `Slewing` true; RoofRoof asks N.I.N.A. to park; the park holds
4 s; the roof opens; Open.

### User Story 3 - The queued open is called off (P1)

While it waits: a person's Stop or the automatic close -> Closed at once; the weather turns unsafe, the
close arms or the wait runs out -> Error (and the description says why). N.I.N.A. waiting for open
stops on it.

### User Story 4 - Close queued behind the park (P2)

The same for `CloseShutter`: Closing while it waits (park, or an upload), Closed at the end, Open if a
person stops it.

### Edge cases

- The weather unsafe as well: the weather's refusal is given (the gate), not the armed one.
- The roof already open: an Open is accepted (nothing to do), armed or not.
- A person's Open from the web UI while armed: unchanged (accepted; the close takes over). F39b's
  choice (refuse it too, or an override that stands the close down) is the owner's.
- A Close is never refused for this.
- The armed flag is handed from the close sequence's step (10 a second) to the roof's checks; a
  stale "armed" after it clears refuses once more with "0 s left".

## Requirements

- **FR-001**: `RoofIo::CommandContext::closeArmed` (`CloseArmed`: armed, ms left, hold length). With
  it set and the weather safe, `checkCommand` refuses a remote (Alpaca, MQTT) Open with
  `CLOSE_ARMED_REASON` ("closeArmed") and `closeArmedText`, before the park gate so it never starts a
  park wait. A person's Open and every Close are unaffected; an Open of an open roof is accepted.
- **FR-002**: `CloseSeq::WeatherHold::clearsInMs()` (the safe spell still needed; the whole hold
  while the last step was unsafe; 0 when not fired) and `lengthMs()`.
- **FR-003**: `CloseOnUnsafe` publishes `closeArmed()` from each step's hold and hands it to
  `RoofControl::setCloseArmed` (atomics, no lock); `checkNow` puts it in the context, so admission,
  the park wait's re-check (`checkWait`) and the run after the wait all see it. The demo core does
  the same from its hold.
- **FR-004**: The Dome's states are unchanged (spec 027): an accepted move not yet started reads
  Opening/Closing with `Slewing` true; called off by a stop or the automatic close, where the roof
  is; a failed wait (time limit, another check, the close arming) Error until the next command.
- **FR-005**: No new task, buffer, setting or alert; three atomics in `RoofControl`, three in
  `CloseOnUnsafe`. REST/MQTT refusals carry the same reason and text; the status already shows the
  park wait and the hold (`weatherHold.fired`). No UI change.
- **FR-006**: Docs: alpaca (shutter state, refusals table, OpenShutter row), close-on-unsafe (weather
  hold), changelog, roadmap; constitution 12.3.0.

## Constitution rules relied on

- III (12.3.0, new sentence): an automation's open is refused while the close is armed.
- IV (12.3.0, new bullet): an accepted move reports its progress from the moment the call returns;
  one that won't run is refused at the call. ConformU runs against the simulator, whose context has
  no armed close: its Dome behaves as before.
- V: the rule is pure and tested natively. VI: no task or buffer.

## Tests

`test_open_reports_opening` (the Alpaca router over a model of RoofControl's admit and step on the
simulated roof): an open queued behind the park reads Opening on the next read and after 1 s,
`Slewing` true; the park held 4 s -> opening, no blip, -> Open; never Closed from the accept to the
open end; queued then a person's Stop -> Closed at once, never runs; the automatic close -> Closed; a
check refusing the wait -> Error; the wait limit -> Error; refused for the weather -> InvalidOperation
with the reason, no wait; park requests off -> InvalidOperation; the armed close -> InvalidOperation
with the text, also not parked (no wait), runs once cleared; MQTT refused, a person and a Close not;
weather unsafe names the weather; the close arming during a wait -> Error, never runs; a close queued
behind the park -> Closing, then Closed; stopped by a person -> Open; a close waiting for an upload ->
Closing; `WeatherHold::clearsInMs` (fired, counting down, a blip restarting it, cleared).

## Owner checks (pending)

- [ ] Bench: fire the weather hold (mock weather unsafe 60 s, then safe), then N.I.N.A.'s Open Dome
  Shutter within the clear spell: N.I.N.A. logs the refusal text; nothing moves. After the spell the
  open runs.
- [ ] Rig: mount unparked, roof closed, N.I.N.A. Open Dome Shutter: N.I.N.A. waits (no "still reported
  as closed"), RoofRoof parks the mount, the roof opens.
