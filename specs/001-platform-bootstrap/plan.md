# Implementation Plan: Platform Bootstrap

**Branch**: `spec/001-platform-bootstrap` | **Date**: 2026-11-10 | **Spec**: [spec.md](spec.md)

## Summary

Import SQMeter @ `877208b` whole, then strip it leaf-first to the platform RoofRoof needs, keeping
the build and tests green after every step. Next, rename the product. Finally add the two
RoofRoof-only pieces: the driver held off at boot (`lib/DriverLogic` + `MotorDriver`) and the
heritage record. Decisions and the coupling map are in [research.md](research.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict; Python 3.12 |
| **Primary Dependencies** | ESPAsyncWebServer 3.12.0, AsyncTCP 3.5.0, ArduinoJson 6, PubSubClient 2.8; Preact, Vite 8, Zod, MSW; Unity, Vitest, Playwright; mkdocs-material 9.7.7 |
| **Storage** | NVS (config, namespace `roofroof`); LittleFS (web UI, gzipped) |
| **Testing** | `pio test -e native`, `npm test` (Vitest), Playwright against the demo, `tools/quality/check.py` |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB), partition layout l2; demo in the browser (WebAssembly core) |
| **Project Type** | Embedded firmware + web UI served by the device + docs site |
| **Performance Goals** | SQMeter's, unchanged (discovery reply < 200 ms, live updates every 2 s) |
| **Constraints** | Image < 75% of the 1.75 MB OTA slot (SC-005); web UI within SIZE-01/02; zero warnings; main loop stack headroom > 2 KB |
| **Scale/Scope** | SQMeter is ~28.7k lines of firmware and ~56k of web; we expect roughly half of each to remain |

## Constitution Check

| Principle | How this plan satisfies it |
|---|---|
| I. Motion Safety | `MotorDriver::holdOff()` is the first statement of `setup()`. `lib/DriverLogic` is the only place that decides pin levels, with native tests for the never-both invariant. No code path applies anything but `Off` in 001. The EN pull-down is documented as required. |
| II. Parked mount | Not applicable yet: there is no close. Nothing in 001 can move the roof. |
| III. Unsafe ends closed | Not applicable yet (specs 004/005). |
| IV. Alpaca | The router stays the single implementation, made generic over a device list (empty). ConformU is suspended until 006 because there is no device to test. This is recorded in Complexity Tracking. |
| V. Pure logic | Every kept `lib/` module keeps its native tests. `lib/DriverLogic` and `lib/HaDiscovery` are new and pure. STRUCT-01 is enforced by check.py. |
| VI. Budgets | `flash_budget.py` runs for `esp32dev`. Size baselines are re-recorded after stripping (they only go down). |
| VII. UI | No new parts; removal and renaming only (spec Design section). |
| VIII. Security | Route auth is unchanged from SQMeter; the registry check stays in CI. |
| IX. Docs | The docs build is a PR gate. Diagrams of removed features are deleted; the kept ones (first setup, OTA, MQTT, alerts, workflow, IPv6) are re-confirmed. |
| X. Quality | check.py with an empty baseline. |
| XI. Heritage | `docs/development/heritage.md` is written in the last phase, from the diff against the import commit. |

**Gate**: pass, with one recorded exception (ConformU, below).

## Project Structure

### Documentation (this feature)

```text
specs/001-platform-bootstrap/
├── spec.md, plan.md, research.md, data-model.md, quickstart.md
├── contracts/  (firmware-marker.md, interfaces.md, schemas/ ← moved from SQMeter spec 016)
├── checklists/requirements.md
└── tasks.md    (next: /speckit-tasks)
```

### Source code (after 001)

```text
platformio.ini        esp32dev + native only
partitions.csv        l2 (4 MB)
include/              Logger, WiFiManager, MQTTClient, AlertDispatcher, TimeManager,
                      WebServer, FirmwareMarker, BuildTarget, version, RoofPins, MotorDriver, ...
src/                  main.cpp, ConfigStore, WebServer{,Api,Alerts,Alpaca,Backup,Live,Status,Updates}.cpp,
                      MotorDriver.cpp, ... (no sensors/, Ble, Oled, Power, LanguagePack, OtaUpdater)
lib/                  AlertLogic, AlpacaLogic (generic), CaptiveDns, ConfigBackup, ConfigModel,
                      ConnectionBudget, DeviceCore (status, alerts, settings), DriverLogic*, FirmwareImage,
                      HaDiscovery*, NetAddress, SettingsDeps            (* new)
test/                 native tests for each lib + test_driver_logic, test_ha_discovery
web/                  SQMeter's UI minus sensor/safety/BLE/display/battery/language/release-check
tools/                quality, api, ui, i18n (en), dashboard, docs, contracts, settings-deps,
                      config-releases, demo-core, alpaca-sim (management only), firmware, release, soak, brand
docs/                 getting-started, hardware (pins, EN pull-down), user-guide, api, development (+ heritage)
.github/workflows/    build, quality, docs (artifact, no Pages), diagrams, release-notes
```

## Phases

Each phase ends with the full local check green:
- `pio run -e esp32dev`
- `pio test -e native`
- `cd web && npm run typecheck && npm test`
- `check.py --fast`

Every phase is its own commit (or a few commits).

1. **Import.**
   - Copy SQMeter @ `877208b` via `git archive`, minus `specs/`, `release-notes/`, `site/` and
     the ignored build output.
   - Keep RoofRoof's own `.specify/memory/constitution.md` and `README.md`.
   - Move SQMeter spec 016's schemas to `specs/001-platform-bootstrap/contracts/schemas/` and
     repoint their consumers.
   - Reset `CHANGELOG.md` to a RoofRoof "Unreleased" section.
   - This is the only commit whose diff is SQMeter's code verbatim.
2. **Builds and boards.**
   - Remove the BLE and S3 envs, the 8/16 MB partitions, BoardProfile's other boards,
     BoardFeatures, and the `build-s3` / BLE CI jobs.
   - Narrow `flash_budget.py` to `esp32dev`.
3. **Bluetooth, OLED, battery.** Remove the drivers, lib/{BleLogic, DisplayLogic, BatteryLogic},
   their config, alerts, routes, UI, tests, docs and catalogue entries.
4. **Sensors and safety.**
   - Remove all sensor drivers, lib/{SkyLogic, RainLogic, WindLogic, SensorTypes, Readings,
     SafetyHistoryLogic}, SafetyHistory, the GPS time source and location.
   - Make the Alpaca router generic and empty (R3), and cut AlertEngine down (R4).
   - Move HA discovery to `lib/HaDiscovery` (R2).
   - Trim the dashboard, Settings tabs, System page, mocks, the demo simulator/panel/tour, the
     inventory, catalogue, glossary, device catalogue and the docs.
   - Rebuild the demo core.
5. **Release check, languages.**
   - Remove OtaUpdater, ReleaseLogic, ReleaseUrls and the update-check UI (R8).
   - Remove LanguagePack, the locales and the translation tooling (R10).
6. **Rename.**
   - Product strings, hostname, hotspot, NVS namespace, MQTT base and client id.
   - Alpaca identity and UniqueID prefix, auth realm, user agent.
   - Backup AAD and key name, localStorage keys, fonts' family suffix, package name.
   - Brand assets (wordmark and roof icon, SQMeter tokens kept), mkdocs, overrides and templates.
   - The firmware marker product field and new magic (R6).
   - Version `0.1.0-dev`.
7. **RoofRoof-only.**
   - `include/RoofPins.h`, `lib/DriverLogic` + `test_driver_logic`, `MotorDriver::holdOff()` first
     in `setup()` (R7).
   - `docs/hardware/wiring.md` (pin table, EN pull-down, SS49E supply note).
8. **CI and docs.**
   - Rework `docs.yml` as a PR gate with artifacts (R9) and the release workflow for a private repo.
   - Remove `alpaca-conformance.yml`.
   - Re-record the size baselines, regenerate OpenAPI and the settings-dependencies page, and
     confirm the remaining diagrams.
   - Write `heritage.md`, and update `CONTRIBUTING`, `BUILD`, `DEV_WORKFLOW` and `SECURITY`.
9. **Verify.**
   - Run the full `check.py` (with clang-tidy), Playwright a11y/layout/dashboard and
     `mkdocs build --strict`.
   - Run the removed-feature grep (SC-004).
   - Do the bench check on a board (SC-001, SC-002) per [quickstart.md](quickstart.md).
   - Run `/speckit-converge`.

## Complexity Tracking

| Violation | Why needed | Simpler alternative rejected because |
|---|---|---|
| ConformU not run in CI during 001 (Principle IV: "every change to the Alpaca code MUST pass ConformU") | With no Alpaca device there is nothing for ConformU to test; it errors on an empty device list | A placeholder device (e.g. a SafetyMonitor) would advertise a device the roof doesn't have, and clients could select it. The router and protocol stay covered by native tests, and spec 006 restores ConformU before any device ships. |
