<!--
Sync Impact Report
- Version: 1.0.0 (initial). Derived from SQMeter's constitution 1.3.1 (SQMeter main @ 877208b).
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

- The driver MUST be off whenever the firmware is not deliberately driving it. EN (GPIO27) low
  means off; both PWM outputs low means stop. EN has a hardware pull-down, so a reset, a crash, a
  brown-out or flashing leaves the driver off. Boot sets EN low and both PWM low before anything
  else runs.
- RPWM (GPIO25) and LPWM (GPIO26) MUST NEVER be driven at the same time. A change of direction
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

- The roof MUST NOT close unless the mount is confirmed parked: N.I.N.A. reports `AtPark` AND
  the park switches on GPIO32/33 are closed, both fresh at the moment the close starts. If the two
  sources disagree, or either can't be read, the mount counts as not parked.
- If the park can't be confirmed, the roof MUST stay open and raise a critical alert. This holds
  even when it's raining: a wet telescope can be dried, a crushed one can't be fixed.
- Every close request is checked the same way, whatever its source: Alpaca `CloseShutter`, the
  web UI, MQTT, Home Assistant or the automatic close. No path skips the check.
- The park interlock is re-checked throughout the close. A mount that reports unparked, or a park
  switch that opens, stops the motor as a motion fault.

### III. Unsafe Ends Closed (Fail-Safe Safety Inputs)

Doubt about the weather MUST resolve to "unsafe".

- Unsafe or raining MUST always end with the mount parked and the roof closed:
  1. stop the running sequence;
  2. ask N.I.N.A. to park;
  3. wait for the park to be confirmed (Principle II);
  4. close.

  Every step has a timeout. A step that times out stops the sequence where it is and raises a
  critical alert naming the step.
- The weather verdict comes from SQMeter: `GET /api/safety` (its `safe` flag and `reasonFlags`,
  bit 9 rain and bit 10 rain-sensor fault) and/or its retained MQTT `<base>/safe`, plus the local
  rain contact on GPIO4 when fitted. Any source that says unsafe makes the verdict unsafe. A
  missing, stale or unparseable source counts as unsafe, and so does a rain sensor that has failed.
- Rain from any source MUST be acted on even if every other input is stale.
- Losing every way to confirm the park while unsafe leaves the roof open and raises a critical
  alert. The alert path MUST NOT depend on the device that failed where another path exists.
- The roof MUST NOT open by itself. Opening is always a person's or a client's command, and it is
  refused while the verdict is unsafe.
- Alerts, MQTT and the web UI report the verdict. They MUST never be able to change it, or the
  motion decisions taken from it.
- Every unsafe reason, fault and refusal MUST state the measured value and the limit, or the
  input that failed (`Supply 10.9 V < 11.5 V`, `Park switch open`, `N.I.N.A. not responding`).

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
- Stop MUST be reachable on every page while the roof is moving, and is never disabled.
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
  PlatformIO Core 6.2 or newer), ESP32 DevKit (WROOM, 4 MB).
  - ESP-IDF options live in `custom_sdkconfig`, and the build fails if one doesn't apply.
  - One build, one partition layout with two OTA slots.
  - ESPAsyncWebServer (handlers match by prefix, so register specific routes before general
    ones) and ArduinoJson 6.
  - Code style: enums and structs over strings, RAII, const-correctness, `std::string` in logic
    code.
- **Web UI:** Preact + Vite, TypeScript strict (no `any`), Zod validation of config, served from
  LittleFS. Firmware and web UI ship as a matched pair.
- **Hardware (reserved pins):**

  | Pin | Use |
  |---|---|
  | GPIO25 / GPIO26 | RPWM / LPWM |
  | GPIO27 | R_EN and L_EN, joined, with a pull-down |
  | GPIO34 / GPIO35 | R_IS / L_IS current sense (optional, 10k/20k dividers) |
  | GPIO36 | 12 V supply sense (100k/22k divider) |
  | GPIO39 | SS49E Hall output |
  | GPIO32 / GPIO33 | park switches (internal pull-ups) |
  | GPIO4 | rain contact (optional) |

  Analog inputs use ADC1 only. Travel length, speed and Hall thresholds are settings, never
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

**Version**: 1.0.0 | **Ratified**: 2026-11-10 | **Last Amended**: 2026-11-10
