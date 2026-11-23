<!--
Sync Impact Report
- Version: 4.1.0 -> 5.0.0 (MAJOR: Principle II no longer always requires N.I.N.A.; the owner may
  make the fitted park switches the only park source).
- Modified: II. Close Only Onto a Parked Mount. The park source is an owner setting
  (`park.source`): N.I.N.A. only, park switches only, or N.I.N.A. and park switches. Settings from
  before it take it from `park.switches` (none: N.I.N.A. only; one or two: N.I.N.A. and switches),
  exactly the rule before. With the switches only, every fitted switch reading closed confirms the
  park; with two, both must; N.I.N.A. counts only against it (a fresh reading that says the mount
  is slewing means not parked). The switches close to ground at park (normally open), so a broken
  wire reads not parked. A switch that hasn't read open over 3 roof openings in a row refuses Open
  until it has been seen open. With no N.I.N.A. reading relied on, the close's re-check reads the
  switches every control step and nothing rides through. The test box's park bypass (it stands in
  for N.I.N.A.) can't be set with the switches only. Principle III's step 2 (ask N.I.N.A. to park)
  is sent only when an address is set.
- New risk (II): with the switches only, a switch that is shorted to ground or stuck closed reads
  "parked" whatever the mount does. Two switches on separate cables make one such fault a
  disagreement (not parked); with one switch nothing can tell it from a parked mount. A fault that
  appears during a night, with one switch, can let the roof close onto an unparked mount; a fault
  that lasts is caught at the fourth roof opening at most (Open refused while the roof is closed).
  Settings, the setup wizard and the docs recommend two switches and warn with one.
- Why: spec 024. An observatory with reliable park switches, or without N.I.N.A. running at night,
  could not close the roof on the switches alone.
- Templates: no change (they read the principles from this file).
- Docs: docs/user-guide/safety-inputs.md, docs/hardware/wiring.md, docs/hardware/commissioning.md,
  specs/024-park-source-and-secrets/spec.md.
- Earlier: 4.0.0 -> 4.1.0 (MINOR: Principle VII is expanded; nothing is relaxed).
- Modified: VII. Quiet, Consistent UI. The design system gains a second documented addition: the
  banner (`Banner` in `web/src/components/ui.tsx`, DS-14), for a state that holds on every page
  (bench firmware, test-box bypasses), built only from the tokens. A bypassed safety check is
  never shown as OK or parked: it reads as bypassed wherever the check is listed.
- Why: the test box banner (spec 022) was a bare line of text outside the page's column; the
  calibration checklists showed "Mount parked: OK" while the park check was bypassed.
- Templates: no change (they read the principles from this file).
- Docs: docs/development/coding-standards.md (DS-14, DS-01), docs/user-guide/test-box.md.
- Earlier: 3.0.0 -> 4.0.0 (MAJOR: Principle III loses a weather source; Principle I's pin and boot
  wording changes with the pins becoming settings).
- Modified: I. Motion Safety. EN, RPWM and LPWM are the pins in effect (a board preset or a
  checked custom map, spec 023), not fixed GPIO numbers. Boot reads the saved settings, then sets EN
  low and both PWM low before anything else; until then the EN pull-down holds the driver off, as it
  does through reset, flashing and the boot loader. A pin set that breaks the pin rules, settings
  that can't be read, or a new device with no board chosen drive nothing: every candidate motor pin
  stays an input and motion is locked out until a valid set is saved and the device restarts. A pin
  change applies only at a restart and is refused while the roof is busy.
- Modified: III. Unsafe Ends Closed. The local rain contact (GPIO4) is removed; rain comes from
  SQMeter (its rain gauge, `reasonFlags` bits 9 and 10, or its MQTT safe topic).
- New risk (III): rain is seen through SQMeter alone. A gauge that misses rain while SQMeter
  stays reachable, fresh and reports no sensor fault is no longer backed by a local contact. A
  SQMeter outage, a stale or unreadable reply, or a reported rain-sensor fault still make the
  verdict unsafe, exactly as before.
- New risk (I): EN is driven low a few tens of milliseconds later in the boot (after the settings
  are read). The driver's off state in that window rests on the EN pull-down, as it already did
  for the several hundred milliseconds of reset and boot loader before it.
- Modified: Platform Constraints. The reserved-pin table becomes the two board presets (ESP32
  DevKit; WT32-ETH01 on the RoofRoof carrier v1, with its Ethernet pins); one image for both.
- Why: spec 023. The roof's controller is a WT32-ETH01 on wired Ethernet, whose Ethernet uses the
  DevKit's motor pins, and the bench stays a DevKit; rain now comes from SQMeter's RG-15 gauge.
- Templates: no change (they read the principles from this file).
- Docs: docs/hardware/wiring.md, docs/user-guide/pins.md, docs/user-guide/safety-inputs.md,
  specs/023-wt32-eth01-board/spec.md.
- Earlier: 2.1.1 -> 3.0.0 (MAJOR: Principles II and III gain a test-box exception, which relaxes
  them).
- Modified: II. Close Only Onto a Parked Mount and III. Unsafe Ends Closed. On a box with no
  telescope under it and no weather station (a controller tried out on a desk), the owner may set
  test-box bypasses: a "no telescope" park source that stands in for N.I.N.A., and a weather bypass
  that stands in for a missing weather source. They exist only as explicit, visible, owner-set
  test-box settings: off by default, changed only by an authenticated settings save (with
  authentication off, as for every setting, any LAN client can make it), shown on every
  page, in the status, MQTT, Home Assistant, the Alpaca description and an alert, and ending at a
  deadline of at most 720 h that needs the clock. Principle III's step 2 (ask N.I.N.A. to park)
  becomes an owner setting, `park.requestPark`, on by default and after an upgrade: off, the
  unsafe close only waits for a park confirmed some other way (a bench box pointed at a live
  N.I.N.A. need not park that mount). Neither can be set alongside the source it stands
  in for (a N.I.N.A. address, an SQMeter source); fitted park switches and a rain contact still
  count; no motion fault, the watchdog or the supply limit can be bypassed (Principle I unchanged).
- New risk (park requests): on a real roof with `park.requestPark` off, rain leaves the roof open
  unless the mount is parked some other way (by hand, or by N.I.N.A.'s own safety monitor); the
  critical "mount not parked" alert says no park was asked for, and the Mount park card warns
  while it is off with a N.I.N.A. address set.
- New risk: a telescope put under a roof still in test-box mode, with no N.I.N.A. address and no
  park switches, can be closed onto until the deadline. A box left in the open with the weather
  bypass on and no station is not closed for weather. Mitigations: the bypasses can't coexist with
  N.I.N.A. or SQMeter, so wiring up the real sources forces them off first; they expire (default
  72 h, at most 30 days); every surface shows them, and an alert goes out when one comes into effect
  and after every restart while one is in effect.
- Why: spec 022. Without it nothing that moves can be tried on a box with no mount and no station:
  jog, teach, the calibration run, Open and Close are all refused, and the bench build has no motor.
- Coordination: this is a MAJOR bump on top of 2.1.1 (spec 021). If spec 021's amendment is taken
  as 3.0.0 instead, this one becomes 4.0.0 with the same text.
- Templates: no change (they read the principles from this file).
- Docs: docs/user-guide/test-box.md, docs/user-guide/safety-inputs.md,
  specs/022-test-box-bypass/spec.md.
- Earlier: 2.1.0 -> 2.1.1 (PATCH: wording). Principle II's re-check during the close now says
  what it checks against: the latest good reading of each source while it is fresh. A N.I.N.A.
  reading that is good, fresh and parked stays in force across a failed read (no reply, an
  unreadable reply, an API error) until it is older than its freshness limit; a failed read is not
  a report that the mount moved. Any report of not parked, a fitted switch that opens or can't be
  read, or evidence past its freshness stops the motor as a motion fault, as before. The start
  rule ("all fresh at the moment the close starts", unreadable counts as not parked) is unchanged.
- Why: on the first board a single N.I.N.A. read timeout during a close latched the park
  interlock and left the roof part-way open (spec 021). The re-check bullet named its stop
  triggers (a mount that reports unparked, a switch that opens) but not what a failed read means;
  the code had read the start rule's "can't be read" into it.
- Risk stated: with no park switches fitted, a mount that unparks while N.I.N.A. stops answering
  can see the roof keep closing until the last good reading is 10 s old. Before, it stopped at the
  first failed reply: typically 2-3 s after that reading when N.I.N.A.'s host refuses the
  connection, up to about 9 s when the request times out (the poller's 2 s period and 6 s request
  limit). So the added exposure is typically about 7 s, at least about 1 s, and up to about 8 s. Park switches still
  stop it at once. Judged a clarification, not a relaxation: no
  reading is used past the freshness limit Principle II already sets, and every named trigger still
  stops the close at once; the owner may choose to treat it as MAJOR.
- Templates: no change (they read the principles from this file).
- Docs: docs/user-guide/safety-inputs.md (Park), specs/021-bench-resilience/spec.md.
- Earlier: 2.0.1 -> 2.1.0 (MINOR: Principle VII is expanded; nothing is relaxed).
- Modified: VII. Quiet, Consistent UI. The design system gains one documented set of onboarding
  components for guided flows (the modal dialog, the progress bar, the choice card group, the
  speech bubble and the mascot) in `web/src/components/ui.tsx`; beyond them, no new button or
  card styles. Any modal over the app carries the Stop bar inside it, so Stop stays visible,
  in the keyboard's focus loop and announced while the rest of the app is hidden behind it.
- Why: spec 018 turns the setup wizard into a one-question-per-screen dialog; built only from
  Settings cards and toggles, it would be the long form it replaces.
- Templates: plan-template.md, spec-template.md, tasks-template.md, checklist-template.md: no
  change (they read the principles from this file).
- Docs: docs/development/coding-standards.md gains DS-11 to DS-13 (the onboarding components) and
  DS-29 (a guided flow's question); specs/018-wizard-redesign/spec.md argues the amendment.
- Earlier: 2.0.0 -> 2.0.1 (PATCH: wording). Principle II's park rule now reads "N.I.N.A. reports
  AtPark AND every fitted park switch (park.switches) is closed". The park switches were always
  optional (the roadmap's design decision, park.switches none/one/two since spec 004, and
  ParkRule confirming on N.I.N.A. alone with none fitted); the old text named GPIO32/33 as if both
  were always fitted. Disagreement or an unreadable source still means not parked. No rule, and no
  behaviour, changes.
- Templates: no change. Docs: specs/ROADMAP.md's design decision, docs/user-guide/safety-inputs.md
  and the setup wizard's park inputs present the switches as an optional second check (spec 019).
- Earlier: 1.0.0 -> 2.0.0 (MAJOR: Principle I's timeout rule gains an exception, which relaxes it).
- Modified: I. Motion Safety, the hard-timeout rule. During an attended calibration run (spec
  016), before travel and speed are measured, a leg's hard timeout is roof.calibrationLimitSeconds
  (30-600 s), enforced by the motion code; later legs are bounded by the times already measured;
  the drive budget and every motion fault still apply.
- New risk: a leg's timeout no longer follows from the roof's own travel and speed. A leg that
  misses its magnet can drive into an end stop for up to the limit (at most 600 s, by default 180
  s) less the travel time; with current sense off, only the timeout stops it. Mitigations: only a
  person at the roof starts a run, every leg at the slow duty from an unknown position, the limit
  is bounded and the owner's setting, later legs are bounded by the measured ones.
- Templates: plan-template.md (Constitution Check) needs no change: it reads the principles from
  this file. spec-template.md, tasks-template.md, checklist-template.md: no change.
- Docs: docs/user-guide/calibration-run.md and specs/016-calibration-run/spec.md cite the rule.
- Earlier: 1.0.0 (initial). Derived from SQMeter's constitution 1.3.1 (SQMeter main @ 877208b).
- Added: I. Motion Safety and II. Close Only Onto a Parked Mount (no SQMeter equivalent);
  XI. SQMeter Heritage (the hybrid copy decision).
- Adapted from SQMeter: III (fail-safe safety inputs), IV (Alpaca, now a Dome), V-X.
- Templates: copied unchanged from SQMeter's .specify/ by spec 001.
-->
# RoofRoof Constitution

The firmware moves a roof over a telescope. A wrong move can crush a mount, and a missed close
can soak it, so the safety principles (I-III) come first. Where two principles conflict, the
lower-numbered one wins, and the controller stops the motor and raises a critical alert instead
of guessing.

## Core Principles

### I. Motion Safety

The motor moves only when the firmware knows where the roof is, why it is moving, and that
nothing has gone wrong.

- The driver MUST be off whenever the firmware is not deliberately driving it. EN low means off;
  both PWM outputs low means stop. EN has a hardware pull-down, so a reset, a crash, a brown-out or
  flashing leaves the driver off.
- EN, RPWM and LPWM are the pins in effect: a board preset, or a custom map that passes the pin
  rules in `lib/` (no reserved, input-only or strapping pin for EN; outputs only where the chip can
  drive). Boot reads the saved settings and then sets EN low and both PWM low before anything else
  runs; until then the EN pull-down holds the driver off, as through reset. Pins change only at a
  restart, and a change is refused while the roof is busy.
- A pin set that breaks the rules, settings that can't be read, or a new device with no board chosen
  MUST drive nothing: every candidate motor pin stays an input, and motion is locked out until a
  valid set is saved and the device restarts.
- RPWM and LPWM MUST NEVER be driven at the same time. A change of direction
  goes through stop (both low) and a dead time first. One function in one module sets the outputs,
  and a native test proves it can never produce both at once.
- The roof MUST NEVER move by itself while its position is unknown. Position is known only at a
  magnet, or estimated from time after leaving one in a move with no fault. Position is unknown
  after a boot away from a magnet, after an abort mid-travel and after a fault. Moving from an
  unknown position needs an explicit, authenticated command from a person, at reduced speed,
  stopping at the first magnet.
- Every move MUST have a hard timeout (the expected travel time from the measured travel and speed
  settings, plus a margin) and MUST stop on its target magnet. The timeout is enforced by the
  motion code itself, never by a client.
  - The one exception is an attended calibration run, which measures travel and speed. Before
    they are measured, each leg's hard timeout is the `roof.calibrationLimitSeconds` setting,
    bounded to 30-600 s and enforced by the motion code. Later legs are bounded by the times
    already measured. The drive budget and every motion fault below still apply, and each leg
    stops at the first magnet it reaches.
- Any motion fault MUST stop the motor, drop EN and lock out motion until a person clears it.
  Motion faults include:
  - a timeout or "no magnet before the timeout";
  - over-current or a stall, when current sense is fitted;
  - the magnet for the wrong end;
  - a Hall reading at either rail (sensor disconnected);
  - supply voltage below its limit;
  - a missed control-loop deadline.

  A restart never clears the lockout.
- Motion control runs in its own task at a fixed rate, independent of Wi-Fi, the web server, MQTT
  and alerts. If the task misses its deadline, the motor stops. A busy network can delay a command;
  it can never prolong a move.
- OTA updates, restarts, a config restore and settings that change motion MUST be refused while
  the roof is moving.
- The decisions behind every rule here MUST live in `lib/` with native tests covering each
  transition and boundary (Principle V). The hardware layer only applies what the logic decides.

### II. Close Only Onto a Parked Mount

Closing onto an unparked telescope is the most expensive failure this device can cause.

- The roof MUST NOT close unless the mount is confirmed parked by its park source (`park.source`),
  all fresh at the moment the close starts:
  - **N.I.N.A. only**: N.I.N.A. reports `AtPark`;
  - **N.I.N.A. and park switches**: N.I.N.A. reports `AtPark` AND every fitted park switch
    (`park.switches`) is closed;
  - **park switches only**: every fitted park switch is closed (one or two; with two, both), and no
    fresh N.I.N.A. reading says the mount is slewing.

  If the sources disagree, or any of them can't be read, the mount counts as not parked. Settings
  saved before the source existed keep the rule they had (no switch: N.I.N.A. only; switches:
  N.I.N.A. and switches).
- **Park switches only.** The switches close to ground at the park position (normally open), so a
  broken wire reads not parked. A switch shorted to ground or stuck closed reads parked, so: two
  switches on separate cables are recommended and a single switch is warned about wherever the
  source is set; and a fitted switch that hasn't read open over 3 roof openings in a row is suspect
  and refuses Open until it has been seen open.
- If the park can't be confirmed, the roof MUST stay open and raise a critical alert. This holds
  even when it's raining: a wet telescope can be dried, a crushed one can't be fixed.
- Every close request is checked the same way, whatever its source: Alpaca `CloseShutter`, the
  web UI, MQTT, Home Assistant or the automatic close. No path skips the check.
- The park interlock is re-checked throughout the close. A mount that reports unparked, or a fitted
  park switch that opens or can't be read, stops the motor as a motion fault. The re-check uses each
  source's latest good reading while it is fresh: a failed N.I.N.A. read is not a report of
  unparked, but it never extends a reading's freshness, and evidence past its freshness stops the
  motor the same way. With the park switches only, no N.I.N.A. reading is relied on: the switches
  are read every control step.
- **Test-box exception.** On a box with no telescope under it, the owner may set the park source
  to "no telescope". Bypasses exist only as explicit, visible, owner-set test-box settings: off by
  default, changed only by an authenticated settings save, shown on every page and every interface
  while in effect, and ending at a deadline the device enforces. "No telescope" stands in for
  N.I.N.A. only: it can't be set while a N.I.N.A. address is, nor with the park switches only,
  fitted park switches still count, and every close path uses the same park result.

### III. Unsafe Ends Closed (Fail-Safe Safety Inputs)

Doubt about the weather MUST resolve to "unsafe".

- Unsafe or raining MUST always end with the mount parked and the roof closed:
  1. stop the running sequence;
  2. ask N.I.N.A. to park (when an address is set, unless the owner turned park requests off,
     `park.requestPark`);
  3. wait for the park to be confirmed (Principle II);
  4. close.

  Every step has a timeout. A step that times out stops the sequence where it is and raises a
  critical alert naming the step.
- The weather verdict comes from SQMeter: `GET /api/safety` (its `safe` flag and `reasonFlags`,
  bit 9 rain and bit 10 rain-sensor fault) and/or its retained MQTT `<base>/safe`. Any source that says unsafe makes the verdict unsafe. A
  missing, stale or unparseable source counts as unsafe, and so does a rain sensor that has failed.
- Rain from any source MUST be acted on even if every other input is stale.
- **Park requests are an owner setting** (`park.requestPark`, on by default and after an
  upgrade). Off, step 2 is skipped and the sequence only waits for a park confirmed some other way;
  without one the roof stays open and the critical alert names the setting. The setting MUST be
  shown on its settings card and wherever a test-box bypass is shown.
- Losing every way to confirm the park while unsafe leaves the roof open and raises a critical
  alert. The alert path MUST NOT depend on the device that failed where another path exists.
- The roof MUST NOT open by itself. Opening is always a person's or a client's command, and it is
  refused while the verdict is unsafe.
- Alerts, MQTT and the web UI report the verdict. They MUST never be able to change it, or the
  motion decisions taken from it.
- Every unsafe reason, fault and refusal MUST state the measured value and the limit, or the
  input that failed (`Supply 10.9 V < 11.5 V`, `Park switch open`, `N.I.N.A. not responding`).
- **Test-box exception.** With no weather source configured at all, the same owner-set test-box
  settings (Principle II) may bypass "no weather source". They never bypass a reading (a rain
  contact that is wet or unreadable) or the doubt of a source that is configured, and can't be set
  while an SQMeter source is on.

### IV. Standards Conformance (ASCOM Alpaca Dome)

N.I.N.A. drives the roof as an Alpaca Dome, and it must behave exactly as the specification says.

- The Alpaca HTTP API lives in `lib/AlpacaLogic` (copied from SQMeter, Principle XI), and the
  firmware and `tools/alpaca-sim` MUST both use it, never a second implementation.
- The Dome implements `OpenShutter`, `CloseShutter`, `AbortSlew`, `ShutterStatus` and `Slewing`,
  with `CanSetShutter` true. Every other capability reports false, and its methods return
  NotImplemented. The advertised interface version MUST match what is implemented.
- `CloseShutter` and `OpenShutter` return at once and report progress through `ShutterStatus`.
  A move the safety rules refuse returns an Alpaca error saying why (Principles II and III). It
  never silently does nothing.
- Every change to the Alpaca code MUST pass ASCOM ConformU (conformance and the Alpaca protocol
  check) with zero errors, issues and configuration alerts, in CI against the simulator.
- ConformU moves the shutter. It runs against real hardware only in bench mode (no motor) or with
  a person at the roof.
- The device UniqueID and discovery MUST stay stable across releases.

### V. Testable Pure Logic

Decision logic MUST be testable on a desktop without hardware.

- The roof state machine, motion profile, timeouts and fault detection, position estimate, Hall
  thresholds, the safety verdict, the close sequence, the park interlock and alert decisions MUST
  live in `lib/*Logic` without Arduino dependencies. Time and inputs come in as parameters, and the
  code is covered by native Unity tests (`pio test -e native`).
- Every safety rule in Principles I-III MUST have a test that would fail if the rule were broken,
  including timeouts, boundaries (`>=` vs `>`) and conflicting inputs.
- Web UI logic and components MUST be covered by Vitest tests, with pages exercised against the MSW
  mocks.
- A bug fix MUST add a test that fails without the fix, unless the fix is in untestable glue, in
  which case the PR states how it was verified instead.

### VI. Embedded Resource Budgets

- Firmware MUST build with `-Wall -Wextra -Werror` and zero warnings.
- The image MUST fit its OTA app slot (CI warns above 90%, fails above 95%; coding standard
  SIZE-04).
- Request handlers on the async TCP task MUST NOT block on the network or on hardware. Outgoing
  HTTP and HTTPS (SQMeter, N.I.N.A., alerts) runs on its own task, with one TLS session at a time.
- Changes that add heap or stack use MUST be measured on a device, keeping the main loop and motion
  task stack headroom above 2 KB.
- Persisted config MUST stay within its NVS limits and be validated before saving.
- The web UI is stored gzipped and MUST meet SIZE-01 and SIZE-02.

### VII. Quiet, Consistent UI

The web UI is used in the dark, often on a phone, sometimes in a hurry.

- The UI follows SQMeter's design system (DS rules, spec 026): the shared parts in
  `web/src/components/ui.tsx` and `settings/controls.tsx`, the tokens in `index.css`, and no new
  button or card styles.
  - The additions are the onboarding set for guided flows such as the setup wizard (DS-11 to
    DS-13): the modal dialog, its progress bar, the choice card group, the speech bubble and the
    mascot; and the banner for a state that holds on every page (DS-14). Each is built only from
    the tokens. A flow or page-wide state uses these rather than a style of its own.
- A safety check that a test-box bypass stands in for is never shown as passing on its own (OK,
  Parked, Safe): wherever it is listed it says it is bypassed.
- Stop MUST be reachable on every page while the roof is moving, and is never disabled.
  - A modal over the app MUST carry the Stop bar inside it, visible above its content, in its
    focus loop and announced when it appears.
- Open and Close show why they are unavailable (unsafe, mount not parked, fault lockout, position
  unknown) through a "?" or a Note. A greyed-out button never appears without its reason.
- Explanations go in "?" tooltips, and copy follows DS-20 to DS-28.
- Every page MUST work at phone width and meet WCAG 2.2 AA (A11Y rules), and every string goes
  through i18n (I18N rules).

### VIII. Trusted-LAN Security

The device is designed for a trusted LAN and has no TLS of its own.

- Every endpoint that moves the roof, clears a fault, changes state, sends something or reveals more
  than readings MUST call `requireAuth`.
- ASCOM Alpaca is open by the Alpaca design. It can move the roof, but only within Principles I-III.
- MQTT commands are honoured only from the configured broker and topics, and pass the same checks
  as every other command.
- Secrets (passwords, keys, tokens, including the N.I.N.A. credentials for API v3) MUST be masked
  in API responses and preserved when the masked value is sent back.
- Nothing may imply the device is safe to expose to the internet.
- The maintainer's private email address MUST NOT appear in any public file.

### IX. Docs Move With Behaviour

- A user-visible change MUST update `docs/` in the same PR: the user guide, the REST, WebSocket and
  MQTT references, and the configuration reference.
- The wiring and commissioning guides MUST match the pin table and the safety behaviour.
- Diagrams of the state machine and the close sequence follow SQMeter's diagram contract (DOC-01).
  They are safety diagrams, so a stale one fails the build.
- `mkdocs build --strict` MUST pass. API changes regenerate the OpenAPI document (API-01).

### X. Code Quality Standards

- All code MUST follow `docs/development/coding-standards.md`, copied from SQMeter's spec 017
  standard, with its rule IDs. Convergence findings and reviews cite them.
- `tools/quality/check.py` MUST pass in CI. The baseline starts empty and may only shrink.
- A rule is set aside only where it doesn't fit, with a same-line suppression naming the rule and
  the reason (EXC-01).
- Generated and vendored code is excluded and never edited by hand.

### XI. SQMeter Heritage

The platform is copied from SQMeter, not shared, because this project has a fixed end date (May
2027).

- Copied modules are listed in `docs/development/heritage.md` with the SQMeter commit they came
  from and whether they have diverged.
- A change to a copied module that would also help SQMeter is marked "upstream candidate" in that
  file. If the project outlives May 2027, those modules move to a shared library.
- Behaviour copied from SQMeter keeps SQMeter's names, field shapes and UI unless this project has
  a reason to differ, and the reason is recorded in the heritage file.
- SQMeter-specific features (sky, environment, wind, Bluetooth, OLED, battery, GPS) are not
  carried over.

## Platform Constraints

- **Firmware:** C++17 on Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 (the pinned pioarduino platform;
  PlatformIO Core 6.2 or newer), ESP32 DevKit (WROOM, 4 MB) or WT32-ETH01 (wired Ethernet), one
  image for both.
  - ESP-IDF options live in `custom_sdkconfig`, and the build fails if one doesn't apply.
  - One build, one partition layout with two OTA slots.
  - ESPAsyncWebServer (handlers match by prefix, so register specific routes before general
    ones) and ArduinoJson 6.
  - Code style: enums and structs over strings, RAII, const-correctness, `std::string` in logic
    code.
- **Web UI:** Preact + Vite, TypeScript strict (no `any`), Zod validation of config, served from
  LittleFS. Firmware and web UI ship as a matched pair.
- **Hardware (board presets; spec 023):**

  | Function | ESP32 DevKit | WT32-ETH01 + RoofRoof carrier v1 |
  |---|---|---|
  | RPWM / LPWM | GPIO25 / GPIO26 | IO14 / IO15 |
  | R_EN and L_EN, joined, with a pull-down | GPIO27 | IO4 |
  | R_IS / L_IS current sense (optional, 10k/20k dividers) | GPIO34 / GPIO35 | IO35 / IO32 |
  | 12 V supply sense (100k/22k divider) | GPIO36 | IO36 |
  | SS49E Hall output | GPIO39 | IO39 |
  | park switches A / B (optional, internal pull-ups) | GPIO32 / GPIO33 | IO33 / IO5 |
  | Ethernet (LAN8720, reserved) | none | IO0, 16, 18, 19, 21, 22, 23, 25, 26, 27 |

  A custom map is checked by the same rules. Analog inputs use ADC1 only. Travel length, speed and Hall thresholds are settings, never
  constants.
- **Compatibility:** config saved by an older release MUST load and be migrated without a factory
  reset, and a migration MUST NOT turn on any motion feature that was off.
- **Updates:** OTA MUST be atomic, and is refused while moving (Principle I).

## Development Workflow and Quality Gates

- Work happens on a branch from `main` and lands through one pull request per feature or fix.
  Nothing is committed directly to `main`, and commits and PRs carry no AI co-author or
  attribution lines.
- A PR merges only when CI passes:
  - the build: typecheck, web tests, native tests, firmware build, LittleFS image, size budgets;
  - ConformU against the Alpaca simulator;
  - `tools/quality/check.py`;
  - the docs build.
- Motion and safety changes MUST be verified on the bench (bench mode, no motor) before they run
  on the roof. The first run on the roof is attended, and the PR records what was checked and
  how.
- Releases are cut by pushing a `v*` tag; `-beta.N` tags are pre-releases.
- New features follow Spec Kit: `/speckit-specify` → `/speckit-plan` → `/speckit-tasks` →
  `/speckit-implement`, repeating `/speckit-converge` until it reports converged. Specs live in
  `specs/<NNN-feature>/`.
- A spec touching motion or safety MUST list the Principle I-III rules it relies on and the tests
  that prove them.
- A spec that adds or changes UI cites the DS rules it relies on and is mocked up and reviewed by
  the owner before it is built. Mockup images stay outside the repo.
- The owner's prompts never go into the repository: specs, plans, tasks, commits and PRs are
  written in their own words.

## Governance

- This constitution takes precedence over other practice in this repository. `CONTRIBUTING.md` and
  the docs MUST NOT contradict it.
- Amendments are made by pull request editing this file, with the version bumped by semantic
  versioning:
  - MAJOR: a principle is removed or redefined incompatibly;
  - MINOR: a principle or section is added or materially expanded;
  - PATCH: wording and clarifications.
- Principles I-III can be relaxed only by a MAJOR amendment that states the new risk.
- `/speckit-plan`, `/speckit-analyze` and `/speckit-converge` check work against these principles.
  A plan that must break one records why in its Complexity Tracking section. Principles I-III
  admit no such exception.
- Each PR description notes which principles the change touches and how they were satisfied.

**Version**: 5.0.0 | **Ratified**: 2026-11-10 | **Last Amended**: 2026-11-23
