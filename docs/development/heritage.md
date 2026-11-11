# Heritage

RoofRoof started as a copy of [SQMeter](https://github.com/DeanJ87/SQMeter), the author's own MIT-licensed ESP32 sky quality meter, taken whole at SQMeter commit `877208b` (RoofRoof's first import commit, `565083b`, "Import SQMeter 877208b") and then stripped back to a platform: WiFi, web server, settings, alerts, Alpaca, MQTT, firmware update, tooling and the web UI shell. This page records what came across, what changed and what was removed, so a fix made in one project can be carried to the other.

Everything below is measured against that import commit: `git diff --stat 565083b..HEAD` and `git log 565083b..HEAD`.

!!! note "Spec numbers"
    RoofRoof has its own specs, 001 to 012 (see `specs/001-platform-bootstrap/plan.md`). Comments that point at SQMeter's specs say "SQMeter spec NNN" so the two numberings are not confused.

## Naming: `namespace SQM` and `SQM_*` stay

The C++ namespace `SQM` and the `SQM_*` build macros (for example `SQM_LAYOUT_ID`) keep their SQMeter names on purpose. Renaming them would touch almost every firmware file and make `git diff` against upstream SQMeter unreadable, which defeats the point of keeping the two projects close enough to share fixes. They are internal names, never shown to a user. Everything a user or another program sees is RoofRoof's: the product name, the firmware marker (`product=roofroof`), the backup file key (`roofroofBackup`), the Alpaca manufacturer, the hotspot and the docs.

## Upstream candidates

Changes that fix or generalise something SQMeter also has, and could be offered back.

| Change | Where | Why it is worth upstreaming |
|---|---|---|
| Firmware upload checks the password before writing | `src/WebServerUpdates.cpp` (`setupOTA`, `requestAuthorised`) | SQMeter checked auth only after the image was written to flash, so an unauthenticated upload was installed and took effect at the next restart. RoofRoof checks on the first upload chunk, before anything is erased or written, and the done handler answers 401. A security fix. |
| `product` field in the firmware marker | `lib/FirmwareImage`, `src/FirmwareMarker.cpp`, `test/test_firmware_image` | Lets a device refuse an image built for another product, not only another layout or build. Contract: `specs/001-platform-bootstrap/contracts/firmware-marker.md`. |
| Generic Alpaca device list | `lib/AlpacaLogic` (`Alpaca::DeviceInfo`, `Backend::devices()`, `MAX_DEVICES`) | SQMeter hard-codes its two devices (ObservingConditions and SafetyMonitor) in the router. A list the backend supplies serves any device type and also covers the zero-device case. |
| Home Assistant MQTT discovery | `lib/HaDiscovery`, `test/test_ha_discovery` | New, small, pure logic with native tests; entities are a table, so any device can add rows. |

## Firmware

SQMeter paths are the same as RoofRoof's unless a different path is shown. "Comments only" means the code is untouched and only comments changed (the sweep that made SQMeter spec numbers explicit).

### `src/` and `include/`

| Module | SQMeter path | State | Upstream candidate |
|---|---|---|---|
| `main.cpp` | `src/main.cpp` | Changed: sensor, display, BLE, battery and update-check setup and tasks removed; the motor driver is held off first thing in `setup()` | No |
| `WebServer.cpp`, `WebServer.h` | same | Changed: sensor, safety, display, language and release routes removed | No |
| `WebServerAlpaca.cpp` | same | Changed: serves the generic device list (no devices until spec 006) | With `lib/AlpacaLogic` |
| `WebServerApi.cpp`, `WebServerStatus.cpp`, `WebServerLive.cpp` | same | Changed: sensor and safety documents removed; `/ws/status` only | No |
| `WebServerAlerts.cpp` | same | Changed: battery and sensor events removed | No |
| `WebServerUpdates.cpp` | same | Changed: GitHub release check removed; upload auth moved before the flash write | Yes (auth fix) |
| `WebServerBackup.cpp`, `WebServerShared.h` | same | Changed: small edits for the removed settings | No |
| `ConfigStore.cpp` | same | Changed: removed settings dropped from load and save | No |
| `TimeManager.cpp/.h` | same | Changed: NTP only, GPS time removed | No |
| `MQTTClient.cpp/.h` | same | Changed: client id is `RoofRoof-%06lX` (was `SQMeter-%06lX`); buffer comment updated; `watch()` subscribes to other devices' topics (SQMeter's safety topics, spec 004), with one callback dispatching commands and watched topics | No |
| `FirmwareMarker.cpp/.h`, `BuildTarget.h` | same | Changed: marker carries `product=roofroof`; one build, no `ble` or `s3` variants | Yes (product field) |
| `RootCertificates.cpp`, `AlertRootCA.h` | same | Changed: GitHub root dropped; alert-service roots kept | No |
| `WiFiManager.cpp/.h` | same | Changed: the setup hotspot `AP_SSID` is `RoofRoof-Setup` (was `SQM-Setup`); `WiFiManager.cpp` is untouched | No |
| `Ipv6Network`, `DualStackClient`, `TlsLock`, `HeapTrace`, `Logger`, `AlertDispatcher`, `BackupCryptoMbed` | same | Unchanged (code), comments only | No |
| `version.h` | `include/version.h` | Changed: `FIRMWARE_NAME` is "RoofRoof"; version reset to 0.1.0 | No |
| `MotorDriver.cpp/.h`, `RoofPins.h` | none | New: the only code that writes the IBT-2 motor driver pins; pin map | No |
| `SafetyPoller.cpp/.h`, `SafetyInputs.cpp/.h` | none | New (spec 004): polls SQMeter and N.I.N.A. on their own task; keeps the weather verdict and park result | No |

### `lib/`

| Library | SQMeter path | State | Upstream candidate |
|---|---|---|---|
| AlertLogic | `lib/AlertLogic` | Changed: battery and sun-position alerts removed; schedule and engine kept | No |
| AlpacaLogic | `lib/AlpacaLogic` | Changed: generic device list in the router; observing-conditions mapper and safety evaluator removed; discovery, client watch and connection memory kept | Yes (device list) |
| CaptiveDns | `lib/CaptiveDns` | Unchanged (code) | No |
| ConfigBackup | `lib/ConfigBackup` | Changed: backup key is `roofroofBackup`; removed settings dropped from the preview and rules | No |
| ConfigModel | `lib/ConfigModel` | Changed: sensor, display, battery, language and board-profile settings removed | No |
| ConnectionBudget | `lib/ConnectionBudget` | Changed: one live endpoint fewer | No |
| DeviceCore | `lib/DeviceCore` | Changed: sky, safety, battery, BLE and display sections removed; alerts and status kept | No |
| FirmwareImage | `lib/FirmwareImage` | Changed: product field in the marker and scanner, RoofRoof's own magic | Yes |
| NetAddress | `lib/NetAddress` | Unchanged (code) | No |
| SettingsDeps | `lib/SettingsDeps` | Changed: catalogue cut to the settings that remain | No |
| DriverLogic | none | New: what the motor driver's inputs must be for each command | No |
| HaDiscovery | none | New: Home Assistant MQTT discovery entities | Yes |
| NinaLogic | none | New (spec 004): N.I.N.A. Advanced API requests and replies behind an interface (v2) | No |
| SafetyInputLogic | none | New (spec 004): SQMeter reply parsing, the weather verdict and the park rule | No |

### `test/`

The native tests follow their libraries. Changed: `test_alert_*`, `test_alpaca_*`, `test_config_*`, `test_connection_budget`, `test_device_core`, `test_firmware_image`, `test_settings_deps`. Unchanged apart from comments: `test_backup_crypto`, `test_captive_*`, `test_net_address`. New: `test_driver_logic`, `test_ha_discovery`, and (spec 004) `test_nina`, `test_safety_input_logic`, `test_sqmeter_source`, `test_park_logic`, `test_config_safety`. Their shared fixtures in `test/fixtures` were cut down to the remaining settings.

## Web UI

The web UI is Preact, Vite and TypeScript with plain CSS (no Tailwind), and keeps SQMeter's design system and tests. The rename to RoofRoof is done.

| Area | SQMeter path | State | Upstream candidate |
|---|---|---|---|
| Dashboard shell, device and status cards | `web/src/dashboard`, `web/src/components/dashboard` | Changed: sky, weather, battery, fault and sun/moon cards removed | No |
| Settings (Alerts, Network, Time, Device tabs, backup card) | `web/src/components/settings` | Changed: Sensors, Safety, Battery, Display and Language tabs and cards removed | No |
| Alpaca, System, Updates pages | `web/src/components` | Changed: no release check; upload only | No |
| Settings dependency logic and links | `web/src/lib/settingsDep*.ts` | Changed: follows the cut catalogue | No |
| Layout, UI kit, toasts, WiFi setup | `web/src/components` | Unchanged apart from the name | No |
| i18n | `web/src/i18n` | Changed: English only; the 13 other language packs and loader removed | No |
| Browser demo and its panel | `web/src/demo` | Changed: runs the firmware core built as `rr-core` (was `sqm-core`); sensor panel, conditions, presets and imaging app removed | No |
| Mocks, types, validation, test helpers | `web/src/mocks`, `types`, `validation`, `test` | Changed: sensor, battery and display types and schemas removed | No |
| Playwright and unit tests | `web/tests`, `web/src/**/__tests__` | Changed: follow the above; language tests removed | No |
| Clock helper | `web/src/lib/clock.ts` | New | No |

## Docs, brand and CI

| Area | SQMeter path | State | Upstream candidate |
|---|---|---|---|
| Docs site, mkdocs config and theme | `docs/`, `mkdocs.yml`, `overrides/` | Changed: pages for removed features deleted; user guide, API and development pages edited; stylesheet renamed `sqmeter.css` to `roofroof.css` | No |
| Hardware docs | `docs/hardware` | Replaced: SQMeter's eight pages (BOM, PCB, enclosure, RG-15, wind and so on) by one wiring page | No |
| Brand kit | `brand/` | Changed: RoofRoof mark, tokens and social images replace SQMeter's | No |
| CI workflows | `.github/workflows` | Changed: language-pack and conformance jobs removed; build, docs, quality and diagram checks kept | No |
| API contracts | `specs/001-platform-bootstrap/contracts` | New: JSON Schemas for RoofRoof's documents, generated by `tools/contracts` | No |

## Tools

| Tool | SQMeter path | State | Upstream candidate |
|---|---|---|---|
| `tools/quality` (coding-standard gate) | same | Changed: paths and baseline | No |
| `tools/settings-deps`, `tools/ui`, `tools/dashboard`, `tools/docs`, `tools/api`, `tools/soak`, `tools/config-releases` | same | Changed: follow the cut catalogue, routes and docs; logic unchanged | No |
| `tools/firmware` (flash budget, USB package) | same | Changed: one build target | No |
| `tools/release` (changelog, notes) | same | Changed: RoofRoof repository and banner | No |
| `tools/demo-core` | same | Changed: emulated device reworked for RoofRoof; sensor feed removed; builds `rr-core` | No |
| `tools/alpaca-sim` | same | Changed: simulator serves no devices until spec 006 | No |
| `tools/i18n` | same | Changed: translation, pack-building and review tooling removed; checker kept | No |
| `tools/brand` | same | Changed: `build.mjs` added | No |
| `tools/contract-check.py` | same | Changed: checks against RoofRoof's schemas | No |
| `tools/contracts` | none | New: schema generator | No |

## Removed

| Module or feature | SQMeter location |
|---|---|
| Light, sky temperature, humidity and pressure sensors (TSL2591, MLX90614, BME280) | `src/sensors`, `lib/SkyLogic`, `lib/SensorTypes` |
| RG-15 rain sensor and rain logic | `src/sensors`, `lib/RainLogic` |
| Wind sensor and aggregation | `src/sensors`, `lib/WindLogic` |
| GPS time and position | `src/sensors/GPSSensor.cpp` |
| Readings document and cloud, dew point and sky quality calculations | `lib/Readings`, `lib/SkyLogic` |
| ASCOM ObservingConditions device and SafetyMonitor evaluator | `lib/AlpacaLogic` |
| Safety monitor and its history | `src/SafetyHistory.cpp`, `lib/SafetyHistoryLogic`, `lib/DeviceCore` |
| Battery gauge, charger and power management | `src/PowerManager.cpp`, `lib/BatteryLogic` |
| Bluetooth LE service and phone alarm | `src/BleService.cpp`, `lib/BleLogic` |
| OLED display | `src/OledDisplay.cpp`, `lib/DisplayLogic` |
| Language packs (13 languages) and their tooling | `src/LanguagePack.cpp`, `lib/LanguageLogic`, `web/src/i18n/locales`, `tools/i18n` |
| GitHub update check and release logic | `src/OtaUpdater.cpp`, `lib/ReleaseLogic`, `lib/ReleaseUrls` |
| Sun position and darkness rules for alerts | `lib/AlertLogic/src/SunPosition.cpp` |
| Board profiles and ESP32-S3 builds | `include/BoardFeatures.h`, `lib/ConfigModel/include/BoardProfile.h` |
| Sky and weather dashboard cards, night chart, sun and moon card | `web/src/components` |
| Demo panel's sensor inputs, conditions and imaging app | `web/src/demo` |
| Hardware docs for the sky meter, indi-allsky guide, sky quality reference | `docs/hardware`, `docs/user-guide`, `docs/reference` |
| Alpaca conformance workflow | `.github/workflows/alpaca-conformance.yml` |
