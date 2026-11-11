# RoofRoof roadmap: specs 002-012

The source of intent for each spec after 001, written from the owner's brief (2026-11-10) and the decisions the owner made. Every spec still goes through Spec Kit (`/speckit-specify` → `/speckit-plan` → `/speckit-tasks` → `/speckit-implement` → `/speckit-converge`). This file says **what each spec covers and where it stops**, so the specs don't overlap or leave gaps.

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

## Owner decisions (don't re-ask)

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

## Autonomy rules (overnight run, 2026-11-11)

- The owner has delegated review and merge to Claude. Every spec ends merged into `main` with CI green.
- Don't stop for questions. Where the brief leaves a choice open:
  - take the **safest** option the constitution allows (stop, stay, alert; never move by itself);
  - record it in the spec under `## Assumptions decided for the owner`, so the owner can review it later.
- Only real hardware facts (measured travel time, magnet thresholds, pin choices beyond the table) stay settings with conservative defaults.
- Anything that needs the physical board or roof goes in the PR as a "Bench check (pending)" checklist. Don't fake it.
- Tooling:
  - PlatformIO is `~/.platformio/penv/bin/pio` (never Homebrew pio).
  - Quality gate: `PATH=~/Projects/SQMeter/.venv-quality/bin:$PATH python3 tools/quality/check.py`.
  - Zero compiler warnings. After any `lib/` change, run `bash tools/demo-core/build.sh`.
  - The quickstart §1 suite (see specs/001-platform-bootstrap/quickstart.md) must pass before a PR.
- Commit style: `NNN: <what>`. Branch: `spec/NNN-<slug>`. PR title: `NNN: <title>`, with the principles touched, the local check results, owner assumptions and bench checks listed.

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

## Merge order

```text
001 ─┬─ 002 ── 003 ─┐
     └─ 004 ────────┴─ 005 ─┬─ 006 ─┐
                            ├─ 007 ─┼─ 008 ── 009 ── 011 ── 012
                            └─ 010 ─┘
```

A spec branches from `main` once its bases are merged. Specs running in parallel each work in their own git worktree. Rebase onto `main` before the PR and again before merging.
