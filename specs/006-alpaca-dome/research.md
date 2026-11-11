# Research: Alpaca Dome

## R1. Which Dome members, and what the rest answer

- **Decision**: IDomeV3 (ASCOM Platform 7, interface version 3). Implemented: `OpenShutter`,
  `CloseShutter`, `AbortSlew`, `ShutterStatus`, `Slewing`, `CanSetShutter` (true), `Slaved` (read
  false, set false accepted), `DeviceState`, and the common members already in the router. Every
  other `Can*` is false; `Altitude`, `Azimuth`, `AtHome`, `AtPark` and the methods `FindHome`,
  `Park`, `SetPark`, `SlewToAltitude`, `SlewToAzimuth`, `SyncToAzimuth` answer NotImplemented
  (0x400); setting `Slaved` true answers NotImplemented.
- **Rationale**: constitution IV lists exactly these members. The ASCOM Dome interface lets
  `AtHome` and `AtPark` throw when homing and parking aren't supported, and ConformU accepts
  NotImplemented for them when `CanFindHome` and `CanPark` are false.
- **Alternatives considered**: `AtPark` = shutter closed (some roll-off drivers do this): rejected,
  it would invite clients to treat the roof's park as the mount's; interface version 2: rejected,
  the router already implements Platform 7's Connect/Disconnect/Connecting/DeviceState.

## R2. Shutter state and Slewing

- **Decision**: closed magnet → `shutterClosed` (1); open magnet → `shutterOpen` (0); opening → `shutterOpening` (2); closing or a waiting close →
  `shutterClosing` (3); stopped part-way, unknown position, fault, or controller not running →
  `shutterError` (4).
  `Slewing` is true while opening, closing or a close waits.
- **Rationale**: ASCOM has no "partly open". Anything that isn't at the closed magnet must never
  read closed: an app that wants the roof closed then closes it. An unknown position is an error
  state for the shutter (ASCOM: "the shutter is in an unknown state"). ASCOM's Dome description says
  `Slewing` is true while any part of the dome, including the shutter, is moving.
- **Alternatives considered**: stopped part-way → `shutterOpen`: rejected after the first safety
  review: an app waiting for "open" would start slewing the mount under a part-open roof.

## R3. Origin of Alpaca commands

- **Decision**: a new `Roof::Origin::Client`. Spec 002 already refuses anything that isn't
  `Person` for a move from unknown and for clearing a fault, so `Client` behaves like `Automatic`
  there. Only a stop from `Person` counts as a person's stop (`RoofIo::countsAsPersonStop`): a
  client's stop must not be able to stand spec 005's close down (constitution III; first review).
- **Rationale**: constitution I needs an authenticated person for both, and Alpaca has no login.
  `Client` also lets the shared command check refuse a client's open while a close runs or waits,
  and keeps the logs and the counting rule explicit.
- **Alternatives considered**: `Person` for Alpaca: rejected (constitution I); a flag beside the
  origin: rejected, one more parameter through every command function.

## R4. Error numbers

- **Decision**: refused move → InvalidOperation (0x40B) with the refusal's text; not connected →
  NotConnected (0x407); Alpaca off → NotConnected with "Alpaca support is disabled in device
  settings" (as SQMeter); not supported → NotImplemented (0x400). A missing or malformed `Slaved`
  value → HTTP 400, as `Connected` already does.
- **Rationale**: InvalidOperation is ASCOM's error for "valid call, not possible in the current
  state", which is what a safety refusal is; N.I.N.A. shows the message.

## R5. The simulator

- **Decision**: the simulator keeps its one-request-per-connection loop and steps the roof on
  simulated time: before each request it advances spec 002's `Roof::Controller` in 20 ms steps up to
  the wall clock, feeding Hall, supply and current readings from `RoofTest::RoofSim` (the plant the
  native tests use). Commands go through the same decision function (`RoofIo::checkCommand`: gates,
  the client rule, the controller's dry run) as the firmware's `RoofControl::precheck`, then the
  controller. Travel 300 mm at 100 mm/s (3 s end to end),
  starting at the closed magnet; options `unsafe` and `unparked` set the gate inputs.
- **Rationale**: no threads, deterministic, and every step of the real motion logic runs (ramps,
  magnets, timeouts). ConformU polls `ShutterStatus` while it waits, which drives the steps.
- **Alternatives considered**: a step thread: rejected, needs locking for no gain; a fake shutter
  without the controller: rejected, the roadmap asks for the simulated roof logic.

## R6. ConformU in CI

- **Decision**: SQMeter's workflow shape: build the simulator, download ConformU (pinned version)
  from the ASCOM release, run `conformance` and `alpacaprotocol` against
  `http://127.0.0.1:11111/api/v1/dome/0`, fail on a non-zero exit, upload the logs. Path filters
  cover `lib/AlpacaLogic`, `lib/RoofLogic`, `lib/RoofIoLogic`, `lib/DriverLogic`, the simulator, the
  plant, the firmware's Alpaca files and the workflow.
- **Rationale**: ConformU exits non-zero on any error, issue or configuration alert.

## R7. Connection rules

- **Decision**: open and close need `Connected` true; abort doesn't; disconnect never stops.
- **Rationale**: ASCOM Platform 7 says members throw NotConnected while not connected; for a stop
  constitution I wins (never refused). A disconnect mid-close that stopped the roof would leave it
  half open.

## R8. The demo

- **Decision**: the demo's emulated device implements the new backend members but keeps an empty
  device list until spec 011's simulated roof.
- **Rationale**: a Dome that can never move would look broken; 011 owns the demo roof.

## R9. A client's stop and the automatic close (safety reviews)

- **Decision**: `AbortSlew` is the roof's Stop (estimate kept) and never a person's stop. A client's
  stop is refused while a close is under way or waiting and the weather verdict isn't safe, whoever
  started the close. With Alpaca off
  the Dome changes nothing, `AbortSlew` included. A client's open is refused while the roof is
  closing, a close waits, or a close was stopped part-way.
- **Rationale**: Alpaca has no login. First review: counting a client's stop as a person's let one
  request stand the close down (III). Second review: even uncounted, a stop interrupts the close
  and spec 005 backs off for 5 minutes, so repeated stops could hold the roof open indefinitely;
  and stop-then-open let a client reverse a person's close. The roof's Abort would make the
  position unknown, which also stops the automatic close (spec 005 FR-010). The web UI's Stop
  (authenticated) always works, so a person can still stop any move. Third review: a rule keyed on
  who started the close was defeated by a client starting its own close (which the sequence then
  only watches) and stopping it; keying the rule on the verdict and the roof's state alone closes
  that, and leaves no origin bookkeeping in the untested glue.
- **Alternatives considered**: let the client stop it and retry without the back-off: rejected, a
  stream of stops still delays the close indefinitely; count only a stop that interrupted a move:
  rejected, it stands the close down mid-close.
