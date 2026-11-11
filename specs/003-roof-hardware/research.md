# Research: Roof Hardware

Decisions behind [plan.md](plan.md). Each records what was chosen, why, and what else was weighed.

## R1. A dedicated FreeRTOS task, `vTaskDelayUntil` at 20 ms

- **Decision**: `roof_motion`, pinned to core 1, priority 5 (above the Arduino loop at 1 and
  AsyncTCP), stack 6 KB (the step's wording and logging keep more than 2 KB spare), released by `vTaskDelayUntil` every 20 ms. Each step: read the
  inputs (outside the lock), then under one mutex: hand over pending settings, the watchdog and
  interlock checks, `tick`, apply the output, copy the snapshot. The fault record is saved after the
  lock is released.
- **Rationale**: constitution I asks for a task of its own at a fixed rate; spec 002's Hall
  confirmation (three readings) and step-gap limit (100 ms) assume 50 Hz. Core 1 keeps it away from
  the Wi-Fi stack on core 0; priority 5 preempts the web server and the loop, so they can delay a
  command but never a step. AsyncTCP 3.5.0 defaults to priority 10 on core 1, which would outrank
  the step, so `platformio.ini` sets `CONFIG_ASYNC_TCP_PRIORITY=3`: above the loop (1), below the
  roof (5), which stays below the esp_timer task (22) that runs the watchdog. (The first version of
  this decision assumed AsyncTCP ran at 3 without setting it; the PR review caught it.)
- **Alternatives**: a hardware timer ISR stepping the logic (ADC reads and a mutex aren't ISR-safe,
  and the logic's floating point would run in the ISR); stepping from `loop()` (shares the loop
  with Wi-Fi, MQTT and alert work, which the constitution rules out); 10 ms (no benefit: the logic's
  constants are tuned for 20 ms).

## R2. Serialising commands with steps

- **Decision**: one FreeRTOS mutex (`xSemaphoreCreateMutex`, priority inheritance) guards the
  controller and the driver writes. A command from the web server's task takes it, calls
  `command`, applies `output()` and releases it. Nothing inside the lock blocks on the network or
  the flash.
- **Rationale**: the contract says to apply `output()` after every tick and command; with one lock
  the last writer always applies the latest decision. Priority inheritance stops the web task
  holding the lock while preempted by something lower than the motion task.
- **Alternatives**: a command queue drained by the task (a stop would wait up to 20 ms; the
  contract asks for the output to change at once).

## R3. The watchdog: an `esp_timer` with an EN block

- **Decision**: a periodic `esp_timer` (10 ms, dispatched from the high-priority esp_timer task on
  core 0) compares `millis()` with the last step's time (an atomic). If the driver is enabled and
  the gap is over 100 ms, it calls `MotorDriver::trip()`, which, inside a spinlock critical section,
  sets a "tripped" flag and writes EN low with `gpio_set_level`. `MotorDriver::apply` sets EN high
  only inside the same critical section and only when the flag is clear, so a step racing the trip
  can never switch the driver back on. The next step sees the flag, latches
  `External` (cause watchdog, measured = the gap, limit = 100 ms), and only then clears the flag
  (the output is then Off). The decision itself (`watchdogExpired`) is a pure, wrap-safe function.
- **Rationale**: independent of the control task (another core, another task), so a hung or
  starved control task can't hold the motor on. EN low is enough to switch the IBT-2 off
  (constitution I).
- **Alternatives**: the system task watchdog alone (its timeout is 30 s, shared with the loop);
  the ESP32 RTC watchdog (resets the chip: EN falls by the pull-down, but the fault cause is lost
  and the reset takes longer). The task watchdog is still added as a second line (FR-009).

## R4. ADC: raw counts for the Hall, calibrated millivolts for the rest

- **Decision**: 12-bit, 11 dB attenuation (about 0.15-3.1 V) on GPIO34, 35, 36 and 39. Hall:
  `analogRead` (raw counts, what spec 002's thresholds and rails are in), median of five samples.
  Supply and current: `analogReadMilliVolts` (eFuse-calibrated), averaged over four samples, then
  scaled by the fixed dividers.
- **Rationale**: the Hall thresholds are calibrated in counts from the live reading (spec 002
  clarification), so no scaling must sit between the reading and the threshold. The median keeps a
  rail reading a rail (a disconnected sensor pulled to 0 by its divider) while rejecting a single
  spike. Volts and amps need the calibration curve to be close to a meter.
- **Alternatives**: averaging the Hall (a spike drags the mean, and an intermittent rail could
  average out of the rail band); oversampling more (each read is about 20 µs; 13 reads per step is
  well under 1 ms).

## R5. The current-sense scale

- **Decision**: amps = max(mV(GPIO34), mV(GPIO35)) / 1000 × 30/20 × `currentAmpsPerVolt`, a new
  setting (default 8.5 A/V, 0.5-100). Only one side carries current in a direction; the larger is
  also the conservative choice.
- **Rationale**: the BTS7960 sense ratio (typ. 8500, wide tolerance) and the IBT-2's sense
  resistor are hardware facts that vary between boards, so they are a setting with a default and a
  calibration step (calibration page).
- **Alternatives**: a fixed constant (a wrong board would silently move the over-current limit).

## R6. Switch inputs: three equal readings, one second of chatter

- **Decision**: per input, a level counts after three equal readings in a row (one per step); until
  the first, or after one second without three equal readings in a row, the input is unreadable.
  Low = contact closed. Park switch: closed = parked. Rain contact: closed = raining.
- **Rationale**: matches the Hall's three-reading confirmation; a bouncing microswitch settles in
  a few ms. A contact that never settles is a fault in the wiring, and constitution II/III treat an
  unreadable input as not parked and unsafe.
- **Alternatives**: a polarity setting for the park switches (rejected: an inverted input reads a
  broken wire as parked, constitution II).

## R7. The fault record in NVS

- **Decision**: namespace `roofroof`, key `roofFault`, a 16-byte blob: magic `R` `F`, version 1,
  kind, cause, a reserved byte, measured (float), limit (float), and a 16-bit checksum (sum of the
  bytes, inverted). Written by the control task after the step that changed it (a fault has
  already switched the driver off, so the flash write can't delay a driven step); removed when the
  fault clears. Decoding: no key = no fault; anything else that doesn't check out = a fault
  "stored fault unreadable" (cause `storedUnreadable`).
- **Rationale**: one small blob is atomic in NVS. A damaged record must never be read as "no
  fault" (constitution I: a restart never clears the lockout).
- **Alternatives**: the config JSON (written by the web task, and a settings save would rewrite
  it); writing from the main loop (a reboot in between would lose it).

## R8. The park and weather gates live in this spec

- **Decision**: `RoofIo::closeGate` (park result parked AND every fitted switch closed, as read by
  the control task) and `RoofIo::openGate` (verdict safe) are checked before `command`. During a
  close, `parkHolds` is re-checked every step and its loss latches `External` (cause
  `parkInterlock`). During an open, a verdict that is no longer safe sends `Stop`. The main loop
  hands the verdict and park result to the control task after each SafetyInputs evaluation (once a
  second); the control task checks its own switch readings every step.
- **Rationale**: constitution II and III apply to every path, and this spec adds the first one
  (REST). Spec 005 builds the automatic sequence on top and reuses the gates.
- **Alternatives**: leaving the checks to spec 005 (would ship a close that skips the park check).
- **The hand-over limit (5 s)**, derived from what can hold up one main-loop pass while the roof
  moves: MQTT reads (and the CONNACK wait) give up after 3 s (`SOCKET_TIMEOUT_S`, was 10 s); no MQTT
  reconnect is tried while the roof moves (a reconnect to a dead broker is DNS + a 3 s TCP connect +
  the CONNACK wait); the web server's work is short, alert delivery has its own task, settings
  saves and command-line OTA are refused or wait while the roof moves, and the WiFi driver keeps no
  NVS copy (`WiFi.persistent(false)`). The worst pass is therefore about 3 s, under 5 s.
  SafetyInputs evaluates once a second, so a park result is at most about 6 s old at the gate,
  plus N.I.N.A.'s own 10 s freshness rule inside it.
- **Residual, by the owner's decision (spec 004)**: the park rule uses only the latest N.I.N.A.
  poll, so one failed poll during a close latches `parkInterlock`; this fails safe (the roof stops
  where it is, a person decides).

## R9. Settings save guard

- **Decision**: `RoofIo::motionSettingsChanged(old, new)` compares `roof` and `park`;
  `RoofIo::refuseSettingsSave(state, changed)` refuses while opening or closing. The web server asks
  the roof before saving (settings page and restore) and answers 409. The roof takes accepted
  settings through a pending slot; the control task calls `applySettings` and keeps them pending
  while it answers `Moving`.
- **Rationale**: the contract's "refuse roof.* saves while opening or closing"; park settings
  change the close interlock, so they are refused too.

## R12. Nothing writes the flash while the roof moves (added after the PR review)

- **Decision**: while the state is opening or closing, settings saves, restores, firmware and web
  UI uploads (refused on the first chunk, before anything is erased), WiFi changes and restarts
  answer 409; a scheduled restart waits; command-line OTA isn't serviced; the alerts pause state is
  saved once the roof stops; MQTT reconnects wait. While an upload is being written, open and close
  are refused (`flashBusy`).
- **Rationale**: a flash erase or write disables the code cache on both cores. The control task
  and the esp_timer watchdog both stall (a sector erase is about 45 ms, worst case a few hundred),
  while EN stays as it was: the watchdog can't drop it during the stall.
- **Residual**: the watchdog shares the stall, so a flash write that slipped through (a race
  between a save and an automatic command from spec 005, or the roof's own fault record, which is
  only written with the driver off) would let the motor run unsupervised for its duration. A later
  hardware timeout (a monostable or RC on EN refreshed by the PWM task) would close it.
- **Alternatives**: IRAM-resident watchdog ISR (a GPTimer ISR in IRAM survives a cache stall, but
  `gpio_set_level` and the step's data would need to be in IRAM too; kept for later with the
  hardware timeout).

## R13. Current sense under PWM (added after the PR review)

- **Decision**: the BTS7960's IS output follows the motor only while its side is on, so the
  reading averaged over 8 samples (several PWM periods) is about duty × the motor current. It is
  scaled back by the duty applied while it was read (`RoofIo::runningAmps`). Driving at 25% duty or
  more for a second, a reading under 0.2 A is a broken sense wire: NaN, which spec 002 latches as
  `currentUnreadable`. The wiring page recommends a capacitor across each divider's lower resistor
  so the ADC sees the averaged value.
- **Rationale**: without the scaling, the over-current limit would be 2.5× higher at 40% duty; a
  broken IS wire read 0 A, which looked valid.

## R14. Roof commands need a header (added after the PR review)

- **Decision**: the four roof routes need `X-RoofRoof: 1` (403 without it); the web UI's
  `roofCommand()` sends it. A cross-site form or simple cross-origin request can't set a custom
  header, so a page in the owner's browser can't move the roof (with or without stored Basic auth).

## R15. The watchdog is itself watched (added after the PR review)

- **Decision**: the watchdog timer stamps a heartbeat on every check; `MotorDriver` sets EN high only
  while it is at most 30 ms old, and the control task latches `watchdogStopped` if it is more than
  100 ms old with the driver on.

## R10. The status section and live updates

- **Decision**: `RoofIo::writeStatus(JsonObject, const Snapshot &)` in the pure library (native
  tests; the demo can reuse it in spec 011). The web server copies the snapshot under the lock and
  writes it outside. A revision counter bumps on every state or fault change; the web server
  broadcasts on a changed revision, at most every 250 ms.
- **Rationale**: the dashboard (009) and MQTT (008) read the same section. Unreadable values are
  `null` (never a made-up number).

## R11. PWM: LEDC at 20 kHz, 8 bits

- **Decision**: `ledcAttach(pin, 20000, 8)` on GPIO25 and 26 after `holdOff()`, duty written with
  `ledcWrite`. Order on every apply: the other direction's duty to 0, then this direction's duty,
  then EN (critical section); off: EN low first, then both duties 0.
- **Rationale**: 20 kHz is inaudible and within the BTS7960's rating; 8 bits matches the logic's
  0-255 duty. 80 MHz / 20 kHz allows up to 11 bits.
