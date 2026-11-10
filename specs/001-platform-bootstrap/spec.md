# Feature Specification: Platform Bootstrap

**Feature Branch**: `spec/001-platform-bootstrap`

**Created**: 2026-11-10

**Status**: Draft

## Context

RoofRoof reuses the SQMeter platform by copying it (constitution XI). This spec produces a
device that:

- boots, joins Wi-Fi, keeps its settings, sends alerts, talks MQTT, updates itself and serves
  SQMeter's web UI;
- passes all of SQMeter's quality gates;
- does nothing roof-specific yet.

Everything after it builds on this base. The source is SQMeter `main` @ `877208b`.

Nothing here moves a motor. Because the first image may be flashed onto a board already wired to
the IBT-2, it MUST still hold the driver off (constitution I).

## Clarifications

### Session 2026-11-10

- Q: The repository is private on the free plan, so there is no anonymous GitHub Releases access
  and no GitHub Pages. How are updates and docs handled? → A: Updates are by file upload (Updates
  page) and USB only; the GitHub update check is left out. Docs and the demo are built and checked
  in CI and kept as workflow artifacts, but not published. This can be revisited if the repo goes
  public.
- Q: Languages? → A: English only. The i18n framework and rules stay (`t()`, `en.json` with
  context notes, I18N lint, locale-aware formatting); the translations and the on-device language
  download are left out.
- Q: Share or copy the platform? → A: Copy now; list the modules worth sharing for after May 2027
  (constitution XI).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Get a new board onto the network (Priority: P1)

The owner flashes a fresh ESP32 DevKit, joins its open "RoofRoof-Setup" hotspot, lands on the
Wi-Fi setup page, picks a network and connects. Afterwards the device is at
`http://roofroof.local`.

**Why this priority**: Nothing else can be developed or tested on the device until this works.

**Independent Test**: From a fully erased board, a phone completes setup without typing an IP
address, and `http://roofroof.local` then loads the dashboard.

**Acceptance Scenarios**:

1. **Given** no saved Wi-Fi, **When** the device boots, **Then** it opens the "RoofRoof-Setup"
   hotspot with a captive portal that lands on Wi-Fi setup (as SQMeter spec 014 FR-001, FR-002).
2. **Given** valid credentials, **When** Connect is pressed, **Then** the device joins, the page
   says where to find it, and it restarts onto the network.
3. **Given** saved Wi-Fi, **When** the network is slow to come up, **Then** the device keeps
   trying and opens the hotspot only after 45 s (SQMeter spec 014 FR-007).

---

### User Story 2 - The driver is off whatever the firmware is doing (Priority: P1)

A board wired to the IBT-2 is powered, reset, flashed over USB, updated over the air and
crash-restarted, and the motor never turns.

**Why this priority**: Constitution I applies from the first image that touches the hardware.

**Independent Test**: With a multimeter or logic analyser on GPIO25, 26 and 27, power-cycle,
reset, flash, OTA and force a crash. EN never goes high and the PWM pins never both go high. At
no point after the bootloader hands over are they anything but low.

**Acceptance Scenarios**:

1. **Given** any boot, **When** the firmware starts, **Then** GPIO27 (EN) and GPIO25/26 (PWM)
   are driven low before Wi-Fi, the file system or the config are touched.
2. **Given** this spec's firmware, **When** any API, MQTT or Alpaca request is made, **Then**
   nothing can set EN high (there is no motion code yet).

---

### User Story 3 - Settings, security, alerts and MQTT work as in SQMeter (Priority: P1)

The owner names the device, sets the time zone, sets up Pushover/ntfy/webhook and MQTT, protects
the settings with a password, and backs up and restores the config. All of it behaves exactly as
on SQMeter.

**Why this priority**: Later specs (alerts, MQTT, Home Assistant) build on these unchanged.

**Independent Test**: For each area, run SQMeter's own acceptance scenarios (specs 008, 011, 013,
033) against RoofRoof, minus anything about sensors.

**Acceptance Scenarios**:

1. **Given** password protection on, **When** a protected endpoint is called without
   credentials, **Then** 401; secrets are masked and preserved (SQMeter spec 011).
2. **Given** an enabled alert channel, **When** Test is pressed, **Then** a test alert is
   delivered and the real per-channel result is shown (SQMeter spec 008 FR-007).
3. **Given** MQTT set up, **When** the device connects, **Then** it publishes availability
   under its base topic (default `roofroof`).
4. **Given** a config backup, **When** it is restored to an erased board, **Then** every setting
   comes back, secrets included if the backup was encrypted (SQMeter spec 033).
5. **Given** an out-of-range value, **When** saved, **Then** the browser and the device reject
   it with the same message (shared range fixture).

---

### User Story 4 - Update by file (Priority: P2)

The owner builds a release, opens Updates and uploads the firmware and the web UI images.

**Acceptance Scenarios**:

1. **Given** a valid image, **When** uploaded, **Then** it is flashed and the device restarts,
   on the first attempt or the page's one retry (SQMeter spec 012 FR-003, FR-003a).
2. **Given** an image for another product or layout (e.g. SQMeter firmware), **When** uploaded,
   **Then** it is refused before anything is written, with a message saying why.
3. **Given** the Updates page, **When** opened, **Then** it offers only file upload; there is no
   release check.

---

### User Story 5 - Every SQMeter quality gate runs from day one (Priority: P1)

A contributor runs one command and gets the same checks CI runs. Every check from SQMeter is
present and passing on the stripped code, with an empty baseline.

**Why this priority**: Starting clean is cheap now and impossible later (constitution X).

**Independent Test**: Open a PR that adds a 120-line function, a `fetch` in a component and an
untranslated string. CI fails three times with rule IDs.

**Acceptance Scenarios**:

1. **Given** the repository, **When** `tools/quality/check.py` runs, **Then** it passes and
   `tools/quality/baseline.json` is empty.
2. **Given** CI, **When** a PR is opened, **Then** all of these pass:
   - the build, typecheck, web tests and native tests;
   - the size budgets;
   - the docs build (`mkdocs build --strict`) and diagram checks;
   - the OpenAPI check;
   - the a11y and layout checks on the demo.
3. **Given** the docs and the demo, **When** CI finishes, **Then** both are available as
   downloadable artifacts.

---

### User Story 6 - Try it without a board (Priority: P2)

A contributor runs the demo in a browser and clicks through every page: the same device logic
runs there (compiled from `lib/`), and nothing leaves the browser (SQMeter spec 016).

**Acceptance Scenarios**:

1. **Given** the demo, **When** every page and link is crawled, **Then** there are no error pages
   and no requests beyond the demo's own files.
2. **Given** the demo, **When** settings are saved, **Then** they validate exactly as on the
   device.

### Edge Cases

- A board previously running SQMeter: its NVS settings are under another namespace and are
  ignored. The device starts as new; the docs say so.
- An SQMeter image uploaded to RoofRoof, or the other way round: refused by the image marker
  (product + layout + build) before writing.
- Alpaca enabled with no devices yet: management and discovery answer with an empty device list.
  Clients find the server but nothing to connect to until spec 006.
- An HTTPS alert channel with low heap: the one-TLS-session rule is kept (constitution VI).

## Requirements *(mandatory)*

### Functional Requirements

**Copied and kept (behaviour as in SQMeter)**

- **FR-001**: The firmware MUST provide SQMeter's Wi-Fi setup, captive portal, reconnect and
  mDNS (SQMeter spec 014) as "RoofRoof-Setup" / `roofroof.local`. IPv6 dual stack is kept as it
  is (SQMeter spec 015).
- **FR-002**: The firmware MUST keep settings in NVS under its own namespace, with SQMeter's
  config model, validation (one shared range fixture for browser and device), migration
  framework and encrypted backup/restore (SQMeter specs 011, 033).
- **FR-003**: Settings security (password protection, `requireAuth` on every state-changing
  route, masked secrets) MUST behave as SQMeter spec 011. Every route is in
  `tools/api/routes.json`, checked in CI.
- **FR-004**: The alert dispatcher (Pushover, ntfy, webhook, MQTT channel) with per-channel
  tests and the recent-alerts list MUST work as in SQMeter spec 008. Only a "Test alert" event
  exists until spec 007 adds roof events.
- **FR-005**: The MQTT client MUST connect, publish retained availability under the base topic,
  and support Home Assistant discovery for what exists now (connectivity), as SQMeter spec 013.
- **FR-006**: Time keeping (NTP, time zone) and the Logger MUST work as in SQMeter. The GPS time
  source is left out.
- **FR-007**: The web server MUST serve the REST API, `/ws/status` and the web UI from LittleFS,
  with SQMeter's connection caps and live-update rules (SQMeter spec 011 FR-008).
- **FR-008**: Alpaca discovery and the management API MUST be served from `lib/AlpacaLogic`,
  with `Manufacturer` "RoofRoof" and no devices. `tools/alpaca-sim` builds from the same code.
- **FR-009**: Firmware updates MUST be by file upload (Updates page) and USB, with SQMeter's
  stall handling and slot-switch retry. Images carry a marker of product (RoofRoof), layout and
  build, and the device refuses a mismatch before writing.
- **FR-010**: The web UI MUST be SQMeter's stack and design system:
  - Preact, Vite, TypeScript strict and Zod;
  - `ui.tsx` and the settings controls, tokens, the DS rules and the A11Y rules;
  - i18n with `en.json` only;
  - the MSW mocks and the WebAssembly demo core built from `lib/`.
- **FR-011**: The quality tooling MUST be carried over and pass:
  - `tools/quality/check.py` with an empty baseline;
  - clang-format/clang-tidy, ESLint/Prettier and ruff;
  - the copy, label, glossary and docs UI-path checks;
  - the dashboard inventory;
  - the UI and firmware size checks;
  - the OpenAPI generator and check;
  - the diagram checks.
- **FR-012**: Spec Kit, the constitution, the coding standard (with SQMeter-only rules
  reworded, not dropped), the issue/PR templates, `CONTRIBUTING.md`, `BUILD.md` and
  `DEV_WORKFLOW.md` MUST be present and describe RoofRoof.

**Removed**

- **FR-013**: The following MUST be removed with their settings, routes, UI, tests and docs:
  - every SQMeter sensor (light/SQM, environment, IR/cloud, RG-15 rain, wind, GPS);
  - sky, cloud and wind logic, the SafetyMonitor and ObservingConditions devices, and safety
    history;
  - Bluetooth, the OLED, the battery and the ESP32-S3 board profiles and builds;
  - the GitHub release check and download;
  - translations and the language download.

  Nothing removed may leave a dead route, setting, string or inventory entry behind.

**New in this spec**

- **FR-014**: At boot, before anything else, the firmware MUST drive GPIO27 low and GPIO25/26
  low, and keep them low. A native test covers the function that sets the outputs. The pin table
  from the constitution is recorded in one header, and the hardware docs state the required
  pull-down on EN.
- **FR-015**: `docs/development/heritage.md` MUST list every copied module with its SQMeter path,
  the source commit (`877208b`), its state (unchanged / adapted / RoofRoof-only) and whether it is
  an upstream candidate.
- **FR-016**: The dashboard MUST show the Status card (Alerts and Data tiles, Imaging app tile
  once an Alpaca device exists) and the Device & Network card. No sensor cards.
- **FR-017**: The docs site MUST build with the same theme as SQMeter's, rebranded RoofRoof:
  - getting started (flash, Wi-Fi setup);
  - hardware (pin table, EN pull-down);
  - settings, alerts, MQTT, updates;
  - REST/WebSocket/OpenAPI reference;
  - development (coding standard, heritage).
- **FR-018**: CI MUST run the build, quality, docs/diagram and demo a11y workflows on every PR,
  and keep the built docs and demo as artifacts (no Pages deploy).

### Key Entities

- **Heritage entry**: SQMeter source path, RoofRoof path, source commit, state, upstream
  candidate (yes/no) and the reason it differs.
- **Image marker**: product, partition layout and build, checked before an upload is written.

## Design *(mandatory for UI changes)*

This spec removes UI and renames it; it adds no new design. The remaining pages are:

- the dashboard (Status card, Device & Network card);
- Settings: Device, Network, Time, Alerts, Security;
- Updates (upload only);
- Wi-Fi setup;
- System.

- **DS rules**: DS-01, DS-02, DS-03, DS-08 (Status card with only Alerts and Data), DS-20..DS-28
  (renamed copy), DS-09 (phone layout).
- **Mockup**: none needed. The owner reviews the page list above in this spec. Roof UI comes
  with a mockup in spec 009.
- **Branding**: SQMeter's tokens, colours and type, with "RoofRoof" as the wordmark and a
  simple roof icon in place of SQMeter's logo.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A fully erased board reaches `http://roofroof.local` through the phone setup flow
  in under 3 minutes.
- **SC-002**: Across 20 power cycles, 5 USB flashes, 5 OTA uploads and 5 forced crashes, EN and
  PWM are never seen high.
- **SC-003**: Every CI workflow passes, with an empty quality baseline and zero compiler
  warnings.
- **SC-004**: A search of the code, UI strings, docs and routes for removed features (sky,
  SQM, cloud, wind, GPS, Bluetooth, OLED, battery, S3, release check, language download) finds
  only the heritage file and the changelog.
- **SC-005**: The firmware image uses less than 75% of its OTA slot, leaving room for specs
  002-012.
- **SC-006**: SQMeter's acceptance scenarios for alerts (008), settings and security (011), OTA
  file upload (012), data interfaces (013), Wi-Fi (014) and backup (033) pass on RoofRoof, minus
  sensor-specific steps.

## Assumptions

- The SQMeter commit is `877208b` (v0.4.0-alpha.2 + wind vane). Later SQMeter fixes are pulled
  in by hand and noted in the heritage file.
- One firmware build (`esp32dev`) plus `native`. The partition layout is SQMeter's 4 MB "l2"
  layout, so OTA slot sizes are known; the marker's product field keeps the two products apart.
- The demo, docs and screenshots keep SQMeter's tooling. Screenshots come from the demo in CI.
- The release workflow builds the images and the changelog as release assets on the private
  repo (downloaded by the owner, then uploaded on the device).
- RoofRoof starts its own version numbering at `0.1.0`.
- SQMeter and RoofRoof can share an MQTT broker; their base topics differ by default.
