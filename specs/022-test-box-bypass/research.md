# Research: Test Box Bypass

## R1 Where the bypass is applied

- **Decision**: once, where the weather verdict and the park result are made: `SafetyInputs::evaluate`
  (firmware) and `EmulatedRoof::evaluateSafety` (demo) call one pure function,
  `Safety::applyTestBox`, right after `evaluateWeather`, `evaluatePark` and `closeHold`. The daylight
  bypass is applied in `Daylight::Watch::update` through a `testBox` flag in its settings.
- **Rationale**: every consumer (the gates of Open, Close, jog, teach and the calibration run, the
  park interlock, the automatic close, the status, the inputs report) already reads those three
  values. Applying the bypass at their source means no path can miss it and no path can apply it
  twice (constitution II: "every close request is checked the same way").
- **Alternatives**: a flag in `RoofIo::GateInputs` checked by each gate (rejected: five call sites,
  the close sequence and the status would each need it); a fake N.I.N.A. reading (rejected: it would
  pretend there is a mount and hide the bypass in every text).

## R2 What the park bypass replaces

- **Decision**: it replaces N.I.N.A. only. With it in effect the park result drops every N.I.N.A.
  reason and counts as parked when no switch reason is left. It is exclusive with a N.I.N.A.
  address: validation refuses the save, and `applyTestBox` ignores the bypass if an address is
  present (N.I.N.A.'s reading is anything but `NotConfigured`).
- **Rationale**: "no telescope under this roof" contradicts a configured N.I.N.A. A reading from a
  real mount is exactly the evidence Principle II exists for; it must never be overridden. Fitted
  switches are physical evidence of a mount and keep counting.
- **Alternatives**: auto-off when N.I.N.A. first reports parked (rejected: it needs a persistent
  latch written by the device, and until N.I.N.A. answers after a restart the bypass would hide a
  real mount); ignore switches too (rejected: a switch is a reading).

## R3 What the weather bypass replaces

- **Decision**: only `ReasonNoWeatherSource`. If it is the only reason and both SQMeter records are
  off, the verdict becomes `Safe` with `bypassed` set. Exclusive with an SQMeter source like R2.
- **Rationale**: Principle III's doubt rule exists because a station that stops answering may be a
  station in the rain. With no station configured there is nothing to doubt. A rain contact is a
  reading and stays honoured.
- **Alternatives**: also bypass a configured source's Waiting/Stale/Unreachable (rejected: that is
  the doubt Principle III is about, and would make "unknown" safe).

## R4 Daylight

- **Decision**: `Daylight::Settings::testBox` → `View::bypassed`; `View::closes()` is false while
  bypassed. Mode, state, text and the daylight alerts are unchanged; `describe` adds "close
  bypassed (test box)".
- **Rationale**: the mode stays the owner's; the close comes back by itself at the deadline.
- **Alternatives**: set the mode to alert (rejected: writes the owner's setting).

## R5 Expiry

- **Decision**: one deadline `testBox.until` (UTC seconds) for all bypasses, stamped by the device in
  the settings save when a bypass turns on or `testBox.hours` is sent: now + hours (1-720, default
  72). A bypass is in effect only with the clock set and now < until. The save is refused with the
  clock unset. `until` is never read from a request body or from a restore (strict input); it is
  read from stored settings only.
- **Rationale**: the safest option that lets a box be tested for days; a set-and-forget bypass ends
  within a month at most. Reading the wall clock each evaluation needs no flash write; leaving the
  expired flags set means the device never writes settings on its own.
- **Alternatives**: optional expiry (rejected: allows forever); uptime-based (rejected: a restart
  resets it, or it needs periodic flash writes); clearing the flags at expiry (rejected: a flash
  write from the loop, and the owner loses sight of what was set).

## R6 Supply

- **Decision**: no supply bypass (spec assumption 4).
- **Rationale**: Principle I lists the supply limit among motion faults; "faults are never
  bypassable"; a USB-only board can't drive the motor anyway. `roof.minSupplyVolts` keeps its range.

## R7 The alert

- **Decision**: `AlertType::TestBox` (`test_box`), through the alert engine like the other roof
  events. `RoofInputs::testBox` carries the bits in effect and the text; the engine remembers the
  bits it has seen since boot and sends when a bit appears that it hadn't seen (its first pass after
  boot sees none, so a bypass in effect at boot is announced after the start-up grace). Level 1-4,
  default 3; 0 is refused on save and raised to 1 on load. Not in the "announced" set kept across a
  reset, so every boot reminds. (Reversed by spec assumption 17: a restart alone sends nothing.)
- **Alternatives**: a critical event (rejected: critical events bypass the master switch and the
  pause by design for II/III failures; this is a reminder, not a failure).

## R8 Visibility surfaces

- Status/MQTT: `roof.testBox` from `RoofIo::writeStatus` (shared by `/api/status`, `/ws/status` and
  the MQTT state document), filled by the caller like `daylight`.
- Banner: a test box banner beside the bench banner, polling `/api/status` every 5 s and at once
  on a settings save (a window event), so no extra WebSocket per page.
- Dome description: `Alpaca::domeDeviceInfo(simulation, testBoxText)`.
- Home Assistant: a `test_box` binary sensor (device class problem) whose state is
  `roof.testBox.active` and whose attributes are the `testBox` object.

## R9 Wizard

- **Decision**: a `testBox` choice screen first on the Hardware page (so `setup.step` and
  `STEP_COUNT` are unchanged). "Test box" sets the three bypasses (the device stamps the deadline);
  "a telescope" clears them. The weather source, weather address and N.I.N.A. screens don't show
  while the park or weather bypass is set.
