# Tasks: Platform Bootstrap

**Input**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: The constitution requires tests for all `lib/` logic (V), so test tasks are included
for the new libraries and kept for the copied ones.

**Organisation**: Stripping is foundational work that every story depends on, so it comes first
(Phases 1-2). The story phases then add what is new for each story and verify it. Every task ends
with the local check green (plan, Phases): `pio run -e esp32dev`, `pio test -e native`,
`cd web && npm run typecheck && npm test`, `check.py --fast`.

## Phase 1: Setup (import)

- [x] T001 Import SQMeter @ `877208b` with `git archive` into the repo root, excluding `specs/`, `release-notes/`, `site/` and ignored build output; keep RoofRoof's `.specify/memory/constitution.md`, `.specify/` scripts and `README.md`
- [x] T002 Move SQMeter spec 016's schemas to `specs/001-platform-bootstrap/contracts/schemas/` and repoint `tools/api/openapi.py`, `tools/contracts/generate_schemas.py`, `tools/dashboard/check.py`, `tools/contract-check.py` and `web/src/demo/__tests__/contracts.test.ts`
- [x] T003 Reset `CHANGELOG.md` to an "Unreleased" RoofRoof section (Vitest reads it for the demo version) and set `include/version.h` to `0.1.0+dev`
- [x] T004 Confirm the import builds unchanged: `pio run -e esp32dev`, `pio test -e native`, `cd web && npm ci && npm run typecheck && npm test`; commit "Import SQMeter 877208b"

## Phase 2: Foundational (strip and rename)

**Checkpoint**: after this phase the tree contains only the platform, named RoofRoof.

### Builds and boards (plan phase 2)

- [x] T005 Remove the `esp32dev-ble`, `[s3]` and `esp32s3-*` envs and the sensor/BLE `lib_deps` from `platformio.ini`; delete `partitions_8MB.csv`, `partitions_16MB.csv`, `sdkconfig.esp32dev-ble` and `include/BoardFeatures.h`
- [x] T006 Reduce `lib/ConfigModel/include/BoardProfile.h` and `include/BuildTarget.h` to the one board, `l2` and the standard build; remove `test/test_board_profiles`
- [x] T007 Narrow `tools/firmware/flash_budget.py` `ENVS` to `esp32dev` and `size-baseline.json` to its entry; remove the `build-s3` job and the BLE build/package steps in `.github/workflows/build.yml`

### Bluetooth, OLED, battery (plan phase 3)

- [x] T008 Remove BleService, OledDisplay and PowerManager (`include/`, `src/`) and `lib/{BleLogic,DisplayLogic,BatteryLogic}` with their tests; drop the `ble`, `display` and `battery` config sections, the battery alert events and the `alertsBattB` blob from `lib/ConfigModel` and `src/ConfigStore.cpp`
- [x] T009 Remove the BLE, display and battery routes from `src/WebServer*.cpp` and `tools/api/{routes,operations}.json`, and the `isBleStartPending()` wait before the rollback confirm in `src/main.cpp`
- [x] T010 [P] Remove the web UI for Bluetooth, the phone alarm, display and battery (`BluetoothCard`, `DisplayCard`, `BatteryCard`, `BatteryRows`, `batteryCard`, `phoneAlarm`, `displaySchema`, `batterySchema`, `mocks/battery.ts`, their tests) and their glance items in `web/src/dashboard/glance.ts`
- [x] T011 [P] Remove the related SettingsDeps catalogue entries and Facts fields (`lib/SettingsDeps/`), the backup restart and pin rules (`lib/ConfigBackup/`), the fixtures and the docs (`docs/user-guide/{ble,display,battery}.md`)

### Sensors and safety (plan phase 4)

- [x] T012 Remove `include/sensors/`, `src/sensors/`, `lib/{SkyLogic,RainLogic,WindLogic,SensorTypes,SafetyHistoryLogic}`, `include/SafetyHistory.h`, `src/SafetyHistory.cpp` and their tests; remove `setupI2C`/`setupSensors` and the sensor glue in `src/main.cpp`
- [x] T013 Remove the `gps`, `rain`, `sensor`, `sky*`, `cloudDetection`, `wind` and `location` sections, the time-source priority and the alpaca thresholds from `lib/ConfigModel` (defaults, JSON in/out, validation) and `test/fixtures/config-ranges.json`; keep only alerts, alpaca, mqtt, ntp and wifi ranges (data-model.md)
- [x] T014 Remove GPS from `include/TimeManager.h` / `src/TimeManager.cpp`: NTP is the only source (research R11)
- [x] T015 Make `lib/AlpacaLogic`'s Router, ClientWatch and ConnectionMemory generic over a device list supplied by the Backend (type, number, name, unique ID `roofroof-<mac>-<type>-<n>`, interface version); delete the SafetyMonitor/ObservingConditions handlers, `SafetyEvaluator` and `ObservingConditionsMapper`; keep `test_alpaca_logic` covering discovery, management, protocol and unknown-device 400s (research R3)
- [x] T016 Cut `lib/AlertLogic` `AlertType`, `AlertInputs` and `AlertRules` to `Test`, the imaging-app (client) events and `AlertsResumed`; update the `alertTypeName` and `ntfyTags` tables, `template-variables.json` and `test_alert_*` (research R4)
- [x] T017 Create `lib/HaDiscovery` from Readings' `discoveryPayload`, availability and `forEachDiscovery`, with an entity table of connectivity and the Alerts switch, plus `test/test_ha_discovery`; then delete `lib/Readings` and `test_readings`
- [x] T018 Reduce `lib/DeviceCore` to status, alerts and settings: remove DeviceSky, DeviceBattery, DeviceBle, DeviceDisplay and the sensor parts of DeviceCore/DeviceSafety; keep AlertDelivery, `buildTestAlert` (generic branch) and `isoUtc`; update `test_device_core`
- [x] T019 Strip `include/WebServer.h` and `src/WebServer*.cpp`: the `Sensors` struct, `refreshSensorSnapshot`, `/ws/sensors`, the safety and sensor routes, SafetyHistory use, and the sensor parts of `saveAndApply`; route registry and operations updated
- [x] T020 [P] Strip the web UI per research R2:
  - Remove `SafetyCard`, `SunMoonCard`, `NightChart`, `skyCards`, `weatherCards`, `tones`, `skySensors`, `SensorsTab`, `SafetyTab`, `darkness.ts`, `useSensorActions`, `sensorNames` and `astro`.
  - Reduce `Dashboard.tsx`, `dashboard/{cards,order}.ts`, `glance.ts`, `Settings.tsx`, `settings/tabs.ts`, the Device/Network/Time/Alerts tabs, `System.tsx` and `Alpaca.tsx`.
  - Reduce `types/{readings,status,config,alerts}.ts` and `validation/configSchema.ts`.
- [x] T021 [P] Strip `web/src/mocks/*`, `test/mockDevice.ts`, `settingsDependencyCases.ts` and the demo (`conditions`, `presets`, `shortcuts`, `simulator`, `panel/SensorInputs`, `panel/DeviceReadout`, `imagingApp`, `tour/steps`); update `web/src/dashboard/inventory.json`, `tests/a11y/inventory.ts` and the Playwright specs
- [x] T022 Rework `tools/demo-core` (bridge, `EmulatedDevice` without SensorFeed) and `tools/alpaca-sim` (management only) for the kept libraries; rebuild the WASM core with `tools/demo-core/build.sh`; update `VERSION` and `SOURCE_HASH`
- [x] T023 [P] Trim the checkers' data:
  - `tools/i18n/glossary/en.json` and `_keep.json`;
  - the `literals.mjs` KEEP/UNITS regexes and `gen_device_catalog.py`'s source list;
  - `context.py` PLACES, `tools/ui/label_check.py` (read `lib/HaDiscovery`), `tools/quality/checks.py` HARDWARE_INCLUDE, and `tools/settings-deps/check.py`'s schema list.
- [x] T024 [P] Remove the sensor, safety and sky docs (`docs/hardware/*`, `reference/sky-quality.md`, `development/{sensors,adr-001-*}.md`, `user-guide/indi-allsky.md`) and their diagrams (DIA-01..05, DIA-10, DIA-12, DIA-13, DIA-15); trim the mixed pages

### Release check and languages (plan phase 5)

- [x] T025 Remove `include/OtaUpdater.h`, `src/OtaUpdater.cpp`, `lib/{ReleaseLogic,ReleaseUrls}`, the `updates/check|apply` routes, `GITHUB_ROOT_CA_PEM` in `src/RootCertificates.cpp`, and `useReleaseUpdate`, `lastUpdateCheck`, `lib/updates.ts` and the release track UI in `web/src/components/Updates.tsx`; point the "network busy" check in `src/WebServer.cpp` at the upload state (research R8)
- [x] T026 Remove `LanguagePack`, `lib/LanguageLogic`, the `language` setting, the `i18n` routes, `web/src/i18n/{locales,loader.ts}`, `LanguageCard`, `useLanguage` and `demo/i18n.ts`; set `languages.ts` to `en` only; delete `tools/i18n/{build_packs.py,translate.py,record.json,review/}` and the non-English glossaries, and the `i18n` / `language-progress` specs; keep `api-locale` and `locale-numbers` against a test-only `de` pack (research R10)

### Rename (plan phase 6)

- [x] T027 Rename the firmware identity:
  - `FIRMWARE_NAME`, hostname `roofroof`, hotspot `RoofRoof-Setup`, NVS namespace `roofroof` (alert-pause namespace `roofroof-alerts`);
  - MQTT topic `roofroof` and client id `RoofRoof-%06lX`;
  - Alpaca Manufacturer/identity, auth realm, user agent, the test-alert text, backup AAD `roofroof-backup/1` and the key name;
  - `deviceName` default "RoofRoof".
- [x] T028 Add `product` to the image marker (`src/FirmwareMarker.cpp` with a new 16-byte magic, and `lib/FirmwareImage` parse/check/verdict) per [contracts/firmware-marker.md](contracts/firmware-marker.md); extend `test/test_firmware_image` with an SQMeter-image case ("Not RoofRoof firmware.") and an other-product case (research R6)
- [x] T029 [P] Rename the web UI identity:
  - `web/index.html`, `Layout.tsx` h1 and tagline, `en.json` strings, `localStorage` keys `roofroof.*`;
  - `package.json` name, the font-family suffix `RR`, `vite.demo.config.ts` and `explorer/index.html`, `demo/links.ts`;
  - the mock hostnames and IDs.
- [x] T030 [P] Replace the brand assets: `brand/` (keep the tokens as `roofroof-tokens.css`; RoofRoof wordmark plus a simple roof icon SVG), `web/public/{brand-mark.svg,favicon.svg,favicon-32.png}`, `docs/assets/{logo,favicon}.svg`, `.github/social-preview.png`, `tools/brand/render.mjs`
- [x] T031 [P] Rebrand the docs: `mkdocs.yml` (name, empty `site_url`, repo URL, `stylesheets/roofroof.css`), `overrides/main.html`, `docs/robots.txt`, `docs/llms.txt`, `tools/docs/llms_full.py`, and `.github/ISSUE_TEMPLATE/bug_report.md` (hardware question)

## Phase 3: User Story 2 - The driver is off whatever the firmware is doing (P1)

**Goal**: EN and PWM low from the first instruction of `setup()`, decided in one pure module.
**Independent test**: [quickstart.md §3](quickstart.md).

- [x] T032 [P] [US2] Create `include/RoofPins.h` with the constitution's pin table (GPIO25 RPWM, 26 LPWM, 27 EN, 34/35 current sense, 36 supply, 39 Hall, 32/33 park, 4 rain)
- [x] T033 [P] [US2] Write `test/test_driver_logic/test_main.cpp` first, covering:
  - `Off` → EN low and both duties 0;
  - Forward and Reverse use one side only;
  - for every command and duty 0-255, RPWM and LPWM are never both > 0 (data-model.md invariant).
- [x] T034 [US2] Implement `lib/DriverLogic` (`DriverCommand`, `DriverOutputs outputsFor(DriverCommand, uint8_t duty)`), pure, with a one-line purpose comment per public item (CMT-02)
- [x] T035 [US2] Implement `include/MotorDriver.h` / `src/MotorDriver.cpp`: the only writer of GPIO25/26/27; `holdOff()` sets them as outputs at `outputsFor(Off)`; call it as the first statement of `setup()` in `src/main.cpp`, before Serial
- [x] T036 [US2] Write `docs/hardware/wiring.md`: the pin table, the required 10 kΩ pull-down on EN (and optional ones on RPWM/LPWM), the dividers, the SS49E supply/divider note, and why EN floats until `setup()` (research R7)

## Phase 4: User Story 1 - Get a new board onto the network (P1)

**Goal**: captive-portal setup as RoofRoof. **Independent test**: [quickstart.md §4](quickstart.md).

- [x] T037 [US1] Update `test/test_captive_portal` and `docs/getting-started/*` (first setup, flashing over USB only, no browser flasher) for "RoofRoof-Setup" and `roofroof.local`; re-confirm DIA-07 with `python3 tools/docs/diagrams.py --confirm DIA-07`

## Phase 5: User Story 3 - Settings, security, alerts and MQTT as in SQMeter (P1)

**Goal**: the kept platform behaves as SQMeter's specs 008, 011, 013 and 033 say.
**Independent test**: [quickstart.md §5](quickstart.md).

- [x] T038 [US3] Make the client (imaging-app) alert events report inactive through SettingsDeps with "No Alpaca device yet" while the device list is empty; add the catalogue entry, the web reason and the test cases (research R4)
- [x] T039 [US3] Verify the config round trip and size limits: `test_config_model`, `test_config_ranges` (browser and device share `test/fixtures/config-ranges.json`), `test_config_backup`, and `test_config_migration` passing with an empty `test/fixtures/config-releases/` (research R5)
- [x] T040 [P] [US3] Update `docs/user-guide/{configuration,security,alerts,mqtt}.md` and `docs/api/{rest,websocket,integrations}.md` to the kept routes and topics ([contracts/interfaces.md](contracts/interfaces.md)); re-confirm DIA-06 and DIA-09

## Phase 6: User Story 4 - Update by file (P2)

**Goal**: upload-only updates, refusing other products' images.

- [x] T041 [US4] Reduce `web/src/components/Updates.tsx` to the firmware and web UI upload cards, with tests for the refusal message; update `docs/user-guide/ota.md` and DIA-08 (sources: the upload path, not `checkForUpdate`)

## Phase 7: User Story 5 - Every SQMeter quality gate runs from day one (P1)

**Goal**: CI runs every gate on PRs, and the docs and demo are artifacts.
**Independent test**: a PR with a 120-line function, a `fetch` in a component and an untranslated string fails three times.

- [x] T042 [US5] Rework `.github/workflows/docs.yml`:
  - It runs on PRs and `main`.
  - It keeps the demo build, screenshots, a11y, vendoring (no esp-web-tools), the diagrams and `mkdocs build --strict`.
  - It uploads `site/` (with the demo) as an artifact.
  - Remove `flash_assets.py`, the external demo push and the Pages deploy (research R9).
- [x] T043 [US5] Rework the `release` job in `.github/workflows/build.yml`:
  - Assets `roofroof-l2-{firmware,littlefs}-<tag>.bin`, `roofroof-l2-usb-<tag>.zip` and `roofroof-openapi-<tag>.json` on the private repo.
  - No language packs and no docs dispatch.
  - Update `tools/release/notes.mjs` `REPOSITORY` and `tools/firmware/usb_package.py`.
- [x] T044 [US5] Make `.github/workflows/quality.yml` run on pull requests as well as `main`; delete `.github/workflows/alpaca-conformance.yml` (plan Complexity Tracking)
- [x] T045 [US5] Re-record the size baselines with `tools/ui/size_check.py --update-baseline` and `tools/firmware/flash_budget.py --update-baseline` (both may only fall), regenerate `docs/api/openapi.json` and `docs/reference/settings-dependencies.md`, and confirm `tools/quality/baseline.json` is empty
- [x] T046 [US5] Rewrite the SQMeter-specific wording in `docs/development/coding-standards.md` (examples and paths, not the rules), `CONTRIBUTING.md`, `BUILD.md` (no BLE/S3/GitHub OTA, partition table l2), `DEV_WORKFLOW.md` (namespace `roofroof`, hotspot) and `SECURITY.md`

## Phase 8: User Story 6 - Try it without a board (P2)

- [x] T047 [US6] Check the demo: the crawl test (no error pages, no outside requests), the contract tests against the moved schemas, and the dashboard tests for the Status and Device & Network cards; update `docs/live-demo.md` (run locally or from the CI artifact)

## Phase 9: Polish and cross-cutting

- [x] T048 Write `docs/development/heritage.md` from `git diff --stat <import commit>..HEAD`:
  - every copied module's SQMeter path, the commit `877208b`, its state and whether it is an upstream candidate (`lib/FirmwareImage` product field: yes; `lib/AlpacaLogic` device list: yes; `lib/HaDiscovery`: yes);
  - the removed list;
  - link it from the docs nav.
- [x] T049 Run [quickstart.md §1-2](quickstart.md) in full (check.py with clang-tidy, Playwright, `mkdocs build --strict`, the removed-feature grep) and fix what it finds
- [ ] T050 Bench check on a board, without a motor, per [quickstart.md §3-5](quickstart.md); record the results (SC-001, SC-002, SC-006) in the PR description
- [ ] T051 Run `/speckit-converge` until it reports converged; open the PR "001: platform bootstrap" against `main`, noting the principles touched

## Dependencies

- Phase 1 → Phase 2 → the story phases. Within Phase 2, the order is builds → BLE/OLED/battery → sensors/safety → release/languages → rename. Each step removes things the next would otherwise trip over.
- US2 (Phase 3) only needs Phase 1 and can start any time after T004. The story phases depend on the rename (T027-T031), except US2.
- T022 (the demo core) comes after every `lib/` change (T008-T018) and before US6.
- T045 comes after everything that changes size or routes.
- T048-T051 come last.

## Parallel opportunities

- T010/T011, T020/T021/T023/T024 and T029/T030/T031 touch different areas (web, tools, docs, brand) and can run side by side once their firmware step is done.
- In US2, T032 and T033 are independent; T034 and T035 follow T033.

## Implementation strategy

- The foundational phase is most of the work. It lands as a series of commits on one branch, each green.
- US2 is the first new behaviour and the one that matters for safety. It can be done early so any bench board is safe.
- The PR is opened only after T049-T051.
