# RoofRoof roadmap: specs 002-018

The scope of each spec after 001 and the design decisions they share. Every spec still goes through Spec Kit (`/speckit-specify` → `/speckit-plan` → `/speckit-tasks` → `/speckit-implement` → `/speckit-converge`). This file says **what each spec covers and where it stops**, so the specs don't overlap or leave gaps.

The constitution (`.specify/memory/constitution.md`) governs everything: Motion Safety (I), Close Only Onto a Parked Mount (II), Unsafe Ends Closed (III), Alpaca Dome/ConformU (IV), Testable Pure Logic (V), Budgets (VI), UI (VII), Trusted-LAN Security (VIII), Docs (IX), Code Quality (X), SQMeter Heritage (XI). A lower number wins a conflict.

## Hardware (fixed facts)

- ESP32 DevKit (WROOM).
- IBT-2 (BTS7960) H-bridge driving a 12 V, 20 rpm worm-gear motor. The worm can't be back-driven, so the roof holds wherever it stops.
- Travel is about 1850 mm at about 38 mm/s, so about 50 s end to end. **These are settings**, measured once the roof is built.
- One SS49E linear Hall sensor reads two magnets on the roof, one at closed and one at open. The magnets have opposite poles, so the reading is high at one end and low at the other. **Which end is high is a setting**, and so are the thresholds. Nothing reads position in between.
- Rubber end stops at both ends.
- Pins (see `include/RoofPins.h`):

  | GPIO | Use |
  |---|---|
  | 25 | RPWM |
  | 26 | LPWM |
  | 27 | R_EN and L_EN, joined (needs an external pull-down) |
  | 34 | R_IS current sense |
  | 35 | L_IS current sense |
  | 36 | 12 V sense (100k/22k divider) |
  | 39 | SS49E (5 V supply, so it needs a divider) |
  | 32, 33 | PARK inputs, optional |
  | 4 | RAIN contact, optional |

- Current sense (34/35) has 10k/20k dividers but **isn't fitted yet**. It's a setting; while it's off, jam detection is "no magnet before the timeout".
- Never drive RPWM and LPWM at the same time. EN low means the driver is off. Both PWM low means stop. `lib/DriverLogic` and `MotorDriver` already enforce this; `MotorDriver` is the only writer of 25/26/27.

## Design decisions

- **Park is confirmed by N.I.N.A. AtPark AND the GPIO32/33 park switches.**
  - The switches are optional, and a setting says whether they're fitted.
  - If the configured sources disagree, or the park can't be confirmed, the roof stays open and a critical alert goes out.
- **N.I.N.A. Advanced API** (github.com/christian-photo/ninaAPI):
  - Target v2 (2.2.x): `GET /v2/api/equipment/mount/park`, then poll `/v2/api/equipment/mount/info` for `AtPark`.
  - Put it behind an interface, so v3 (`POST`, 202, Basic auth) can be added later.
  - Check the current docs and record the exact endpoints and fields in that spec's research.md.
- **Safety source:** SQMeter `GET /api/safety` (a `safe` flag plus `reasonFlags`; bit 9 is rain, bit 10 is a rain-sensor fault; fail-safe) and/or SQMeter's MQTT safe topic. The local RAIN contact on GPIO4 is optional. Read SQMeter's spec 013 contract in ~/Projects/SQMeter for the exact payloads.
- **Home Assistant MQTT discovery: yes**, extending `lib/HaDiscovery`, with a `cover` entity for the roof.
- **English only** (the i18n framework stays). **Manual updates only** (upload, no GitHub check). The repo is private, and docs and demo are CI artifacts.

## Specs

Each spec lists its scope, the spec it's built on, and what it leaves out.

### 002 Roof state and motion logic (base: 001)

- **Scope:**
  - `lib/RoofLogic` (or similar), pure and native-tested:
    - the state machine: open, closed, opening, closing, stopped part-way, error, unknown;
    - commands: open, close, stop, abort;
    - the PWM ramp profile, with a soft start and slowing near each end based on the time estimate;
    - stop on the target magnet;
    - the per-move timeout: expected time plus a margin (a setting);
    - jam detection: an over-current threshold when current sense is enabled, otherwise no magnet before the timeout;
    - the position estimate from elapsed time mid-travel;
    - the fault latch: any motion fault stops, drops EN and locks out motion until it's cleared;
    - "never move by itself with the position unknown", so at boot between the magnets the position is unknown.
  - Inputs are plain values (Hall reading, current, supply volts, time). Outputs are a `DriverCommand` and a duty for `lib/DriverLogic`.
  - Settings in ConfigModel: travel mm, speed mm/s, timeout margin, ramp times, Hall thresholds and polarity, current-sense enable and threshold.
- **Out of scope:** pins and ADC (003), safety sources (004), the close-on-unsafe sequence (005), the UI.

### 003 Roof hardware (base: 002)

- **Scope:**
  - The firmware glue that runs 002 on the board:
    - ADC reads for the Hall sensor (39), current (34/35, when enabled), and the supply (36), scaled by the divider and with a 12 V-low threshold;
    - the PARK inputs (32/33) and their polarity;
    - the RAIN input (4);
    - a loop or task at a fixed rate that feeds RoofLogic and drives MotorDriver;
    - the boot rule: unknown unless a magnet is read, with no motion;
    - a watchdog: if the logic stalls, EN drops.
  - Status fields in `/api/status` and `/ws/status` for roof state, position, Hall raw, current, supply and faults.
  - REST open, close, stop and clear-fault commands, auth-protected.
  - Docs: the wiring page and a calibration procedure.
- **Out of scope:** Alpaca, MQTT and UI cards.

### 004 Safety inputs (base: 001, parallel with 002/003)

- **Scope:**
  - `lib/SafetyInputLogic`, pure: combine the sources into one verdict (safe / unsafe with reasons / unknown).
  - The sources:
    - SQMeter `/api/safety` polled over HTTP, with a timeout and a stale timeout, where stale means unsafe;
    - the SQMeter MQTT safe topic;
    - the RAIN contact (a reading supplied by 003; until then, take it as a plain input value).
  - Settings for which sources are used and their URLs and topics; fail-safe throughout.
  - The N.I.N.A. Advanced API client behind an interface (v2 now): park the mount, and read AtPark, connected and slewing. Pure request/response parsing goes in lib, with native tests against recorded JSON fixtures.
  - The park-switch combination rule (N.I.N.A. AND the switches when fitted, disagreement is not parked), as a pure function.
- **Out of scope:** acting on the verdict (005).

### 005 Close-on-unsafe sequence (base: 003 + 004)

- **Scope:**
  - The pure orchestrator: when unsafe and the roof isn't closed:
    1. Ask N.I.N.A. to park.
    2. Wait for the confirmed park (with a timeout).
    3. Close, and confirm closed.
  - Failure rules:
    - park not confirmed → stay open plus a critical alert (constitution II);
    - a close fault → alert.
  - Never open automatically. While it's unsafe, manual opening is refused (unless the owner's constitution allows an override; check it).
  - Wire it to 003 and 004. Events go out through the existing alert pipeline, but the roof alert event types are 007.
  - Exhaustive native tests of the sequence, including timeouts and mid-sequence changes back to safe.
- **Out of scope:** user-facing alert wording and settings (007).

### 006 Alpaca Dome (base: 005)

- **Scope:**
  - An ASCOM Alpaca Dome device through the generic Alpaca router from 001 (the device list stops being empty):
    - OpenShutter, CloseShutter, AbortSlew, ShutterStatus, Slewing, CanSetShutter true;
    - Connected, DeviceState, and the common members;
    - everything else reported as not implemented, per the ASCOM spec.
  - Opening through Alpaca obeys 005's unsafe refusal.
  - Bring back ConformU:
    - `.github/workflows/alpaca-conformance.yml`, against `tools/alpaca-sim` with the simulated roof logic;
    - remove the Complexity Tracking exception.
  - The imaging-app client watch and alerts (from SQMeter) now apply to the Dome.
  - The Alpaca docs page, and DIA-18 updated.

### 007 Roof alerts (base: 005, parallel with 006/010)

- **Scope:** new alert events with levels, wording and templates, following the SQMeter alert design:
  - mount didn't park;
  - roof didn't close;
  - move fault (timeout or jam);
  - 12 V low;
  - unsafe: closing;
  - roof closed after unsafe;
  - safety source lost.
- Also the settings UI rows for them, the settings-deps catalogue entries, and the tests and docs.

### 008 Data interfaces (base: 006 + 007)

- **Scope:**
  - MQTT topics: state, position, motor current, supply voltage, Hall reading and faults out; open, close and stop commands in, auth-gated by the existing MQTT trust model.
  - HA discovery: a cover entity plus sensors (position, current, supply, Hall) and a fault binary_sensor.
  - REST and WebSocket documents finalised.
  - OpenAPI regenerated, contract schemas regenerated, interfaces docs updated.

### 009 Dashboard and settings (base: 008)

- **Scope:**
  - A dashboard roof card in SQMeter's design system:
    - state, position bar, Open/Close/Stop buttons (Stop always available, others disabled with the reason);
    - fault and clear-fault;
    - safety verdict and park state;
    - the live Hall reading, with the thresholds drawn for calibration.
  - The Status card integrates roof items.
  - A Roof settings tab for travel, speed, timeouts, ramps, Hall and current settings, plus the safety sources and N.I.N.A. connection.
  - a11y, inventory, screenshots, Playwright and docs.

### 010 Wi-Fi and OTA under motion (base: 005, parallel with 006/007)

- **Scope:** what the platform does while the roof moves:
  - OTA uploads, restarts and settings saves that need a restart are refused or deferred while moving;
  - Wi-Fi loss mid-move: the move completes or stops safely, and is never left driving without control;
  - captive-portal behaviour when the roof is open;
  - a brown-out or reboot mid-move gives unknown position, with no motion.
- Platform race from spec 004 (FR-011a): `MQTTClient::updateConfig` runs on the web server's task during a settings save. Move the reconnect onto the main loop, like the 004 settings hand-off.
- Tests and docs for each.

### 011 Demo and bench mode (base: 009)

- **Scope:**
  - The demo gets a simulated roof:
    - RoofLogic in the WASM device core, driving a plant model (motor, magnets, Hall);
    - the demo panel can inject unsafe weather, rain, a park failure, a jam and 12 V low.
  - A firmware bench mode: no motor (EN held low) and simulated motion, for trying the UI and Alpaca on a bare board.
  - The tour updated, and contract tests.

### 012 Commissioning guide (base: 011)

- **Scope:**
  - Docs for building and commissioning:
    - wiring checks and the EN pull-down check;
    - the first power-up with the motor disconnected;
    - Hall calibration with the live reading;
    - measuring travel time and speed and setting them;
    - testing the timeouts and the jam stop;
    - the park-switch and N.I.N.A. setup, then the close-on-unsafe dry run;
    - ConformU against the real device.
  - A printable checklist, troubleshooting, and the README finalised.

### 013 Daylight failsafe (base: 011)

- **Scope:**
  - A daylight setting: off, alert, or alert and close, with a sun-altitude threshold and the
    site's latitude and longitude; the sun's altitude from SQMeter's solar position code.
  - Alert and close reuses spec 005's sequence (park, close only on a confirmed park, critical
    alerts on timeouts); it never overrides a person's stop or a halt and never opens. Opening
    is refused while it wants the roof closed. Unsafe weather keeps its behaviour.
  - Its own alert events (spec 007's design), the status document, the Roof card, Settings →
    Roof, the demo's "Sun up" injector and the docs.
- **Out of scope:** MQTT and Home Assistant entities for daylight; any open by schedule.

### 014 Configurable policies (base: 013)

- **Scope:**
  - An audit of every owner assumption of specs 002 to 013 and the timings in `lib/`: safety
    invariants (constitution I-III, VIII) stay fixed; policy and tuning values become settings
    with today's value as the default and a range that can't break an invariant; hardware facts
    are already settings.
  - The settings in ConfigModel, the web schemas, the shared ranges and the settings-deps
    catalogue; an Automatic close card in Settings → Roof with an Advanced fold.
  - The N.I.N.A. opening path made plain (Alpaca stays off by default) at one doc anchor the
    setup wizard links to.
  - Native tests pin every default; a differential fuzz against `main` with default settings.
- **Out of scope:** changing any safety invariant; the setup wizard (015).

### 015 Setup wizard (base: 013)

- **Scope:**
  - A guided set-up of the essential settings once the device has joined a network: device name
    and password, site and time, the hardware with live readings, the Hall sensor, the roof's
    motion with first-run values, the weather sources and N.I.N.A. with a Test, the alert
    channel with a test, the daylight mode, and a review of every check.
  - A setup state (new, offered after an upgrade, done): a new device opens the wizard, an
    upgraded one gets a banner; skip, resume and run again from the System page.
  - Every page saves through the settings API (refused while the roof moves); a read-only
    `GET /api/roof/inputs` for the Tests; the demo restarts it from its panel.
- **Out of scope:** any motion test, Open or Close (they stay in the commissioning guide); the
  advanced settings, which stay in Settings.

### 016 Calibration run (base: 015)

- **Scope:**
  - An attended calibration run, started only by a person from the web UI who confirms they are
    at the roof: from the closed end, a slow open and a slow close (and, if asked, an open and a
    close at the top duty), each leg bounded by its own time limit (a setting), the existing jam,
    over-current and supply checks and the drive budget, stopping at the first magnet.
  - What it measures: each leg's time at a known duty, the Hall reading at rest at each end, the
    in-between level and its noise, and the current and supply while driving.
  - What it proposes: the speed (from the owner's measured travel), the timeout margin, which end
    reads high and the Hall thresholds, each beside the value it would replace. Nothing is saved
    until the owner accepts, and then only through the settings save path.
  - Refused while the weather isn't safe, daylight wants the roof closed, the mount isn't parked,
    the close sequence is busy, a fault is latched, or anything that already blocks an open.
  - The pure run in `lib/`, a firmware route (start, abort, status), a Calibrate card in Settings
    → Roof, a wizard page after the Hall page that links to the attended run page, the demo, and
    the commissioning guide's stages 3 and 8.
- **Out of scope:** a per-move telemetry log (spec 017); proposing current limits; any run
  started by Alpaca, MQTT, the close sequence or a schedule.

### 017 Advanced diagnostics (base: 015)

- **Scope:**
  - An opt-in mode (`diag.enabled`, off by default) that exposes what the controller measures:
    a live document (`GET /api/diag`) and stream (`/ws/diag`, 4 a second while moving) with the
    supply and its sag, current and power, duty, direction and ramp phase, the driver's EN and
    PWM, the Hall reading, the position estimate and times, the drive budget, the control task's
    step time and gap, the watchdog margin, the loop pass, memory, stacks, Wi-Fi, uptime and reset
    reason.
  - A log of the last 5 moves with summaries (peak, average and stall current, lowest supply,
    sag, Hall at each end, result) and downsampled profiles; 32 summaries, optionally kept across
    restarts (spec 010's flash-write rules); CSV and JSON export; clearing by a person.
  - Optional MQTT (`<base>/diag`, `<base>/diag/move`), never while the roof is busy.
  - A Diagnostics page reached from the System page; the demo's simulated roof feeds it.
  - Recording runs in the control step after the output is applied, bounded and allocation-free;
    with the mode off nothing is computed, sent or stored.
- **Out of scope:** any decision from diagnostics (wear alerts, limits); Home Assistant entities;
  the calibration run itself (spec 016); its legs are recorded and tagged as calibration moves.

### 018 Setup wizard redesign (base: 017)

- **Scope:**
  - The setup wizard as a modal dialog over the dashboard: a dimmed, blurred backdrop, a progress
    bar with Back and Close, one question per screen in a speech bubble beside RoofRoof's dog
    mascot, choice cards for the enumerated answers and compact fields beside live readings for
    the rest, a sticky Continue that waits for an answer, Skip for now, and a "Why this matters"
    panel; a review and a done screen.
  - The modal, progress bar, choice card group, speech bubble and mascot as documented
    design-system components; constitution VII amended (2.1.0) to admit them and to require the
    Stop bar inside any modal.
  - Every spec 015 guarantee kept: the same settings and save path, saves refused while moving,
    Tests from the device's own poll, no roof command, resume, skip, run again, the banner.
  - Accessibility (focus trap, Escape asks, focus restored, radio groups, reduced motion) tested
    on every screen at 320 px and desktop with light and dark browser preferences.
- **Out of scope:** any device, setting or API change; a light theme.

## Merge order

```text
001 ─┬─ 002 ── 003 ─┐
     └─ 004 ────────┴─ 005 ─┬─ 006 ─┐
                            ├─ 007 ─┼─ 008 ── 009 ── 011 ─┬─ 012
                            └─ 010 ─┘                     └─ 013 ─┬─ 014
                                                                  └─ 015 ─┬─ 016
                                                                          └─ 017 ── 018
```

A spec branches from `main` once its bases are merged. Specs running in parallel each work in their own git worktree. Rebase onto `main` before the PR and again before merging.
