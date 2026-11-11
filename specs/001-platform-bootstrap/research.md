# Research: Platform Bootstrap

Findings come from two surveys of SQMeter @ `877208b`: one of the firmware (`platformio.ini`,
`src/`, `include/`, `lib/`, `test/`, `tools/demo-core`, `tools/alpaca-sim`) and one of everything
else (`web/`, `tools/`, `docs/`, `.github/`). Each decision below says what was chosen, why, and
what else was considered.

## R1. Copy by subtraction, not by assembly

- **Decision**: Import the whole tree at `877208b` in one commit (no SQMeter history). Then strip
  it in ordered, leaf-first steps, keeping `pio run`, `pio test -e native`, `npm run typecheck`
  and `npm test` passing after each step.
- **Rationale**: About 20 checkers cross-reference each other: the dashboard inventory, the
  settings-deps catalogue, the route registry, the OpenAPI, the glossary, the device-message
  catalogue, the diagrams, the size baselines and the demo-core hash. Assembling modules into an
  empty repo would mean re-deriving all those links by hand. Deleting from a working tree lets
  each checker report what is left dangling.
- **Alternatives**:
  - Assemble only the kept modules: rejected for the reason above.
  - Import SQMeter's git history: rejected. It brings 180+ PRs of sensor history and the
    blame-ignore file for no benefit in a repo that ends in May 2027. The import commit message
    and `heritage.md` record the source commit instead.

## R2. What stays, what goes

Coupling is concentrated in four places: `lib/ConfigModel` (Config.h), `src/WebServer*` +
`include/WebServer.h`, `lib/DeviceCore`, and `lib/AlpacaLogic`'s Router, which is hard-wired to
two devices. Everything else is clean or nearly clean.

| Area | Kept as is | Adapted | Removed |
|---|---|---|---|
| Firmware | Logger, HeapTrace, TlsLock, DualStackClient, Ipv6Network, lib/NetAddress, lib/CaptiveDns, lib/ConnectionBudget, lib/FirmwareImage (plus a product field) | WiFiManager (name), MQTTClient (client id, publish flags), TimeManager (no GPS), ConfigStore (namespace, no battery blob), AlertDispatcher/AlertDelivery (event tables), lib/ConfigModel, lib/ConfigBackup, lib/SettingsDeps, lib/AlertLogic, lib/AlpacaLogic, lib/DeviceCore, WebServer*, main.cpp | include/sensors, src/sensors, lib/{SkyLogic,RainLogic,WindLogic,SensorTypes,BatteryLogic,BleLogic,DisplayLogic,SafetyHistoryLogic,LanguageLogic,ReleaseLogic,ReleaseUrls,Readings}, BleService, OledDisplay, PowerManager, SafetyHistory, LanguagePack, OtaUpdater, BoardFeatures, S3 partitions |
| Web | ui.tsx, controls.tsx, toast, Masonry, WifiSetup, AlertsBell, FaultCard, alert settings cards, BackupCard, most hooks and lib | Layout, Dashboard + cards/order, glance.ts, DeviceCard, Settings + tabs.ts, Device/Network/Time/Alerts tabs, System, Updates, Alpaca page, types, configSchema, mocks, demo | sensor and safety cards and tabs, Sun & Moon, battery, display, BLE, language card, release-check UI, 13 locales |
| Tools | quality, ui, api, docs, contracts, dashboard, settings-deps, soak | i18n (en only), config-releases, demo-core, alpaca-sim, firmware (envs), release (private repo), brand | language packs and translation, flash_assets (needs public releases) |

Readings (`lib/Readings`) is removed, but its Home Assistant discovery scheme (`discoveryPayload`,
availability, `forEachDiscovery`) moves to a small `lib/HaDiscovery`. Spec 008 adds the roof
entities to it.

## R3. Alpaca with no devices

- **Decision**:
  - Make `Alpaca::Router` take its device list from the Backend (type, number, name, unique ID,
    interface version) instead of the fixed `DEVICE_COUNT = 2`.
  - Remove the SafetyMonitor and ObservingConditions handlers, `SafetyEvaluator` and
    `ObservingConditionsMapper`.
  - Keep discovery, the management API, protocol handling, ClientWatch and ConnectionMemory, all
    generic over the list.
  - In 001 the list is empty.
- **Rationale**: Spec 006 adds the Dome as one list entry instead of rewriting the router. Unique
  IDs become `roofroof-<mac>-<type>-<n>`.
- **ConformU**: it needs a device to test, so `alpaca-conformance.yml` is removed in 001 and
  restored in 006 with the Dome simulator. Router and protocol behaviour stays covered by the
  native tests in `test_alpaca_logic`, minus the SafetyMonitor/ObservingConditions files.
- **Alternatives**: keep the SafetyMonitor as a placeholder device. Rejected: it would advertise a
  device that has nothing to do with the roof.

## R4. Alerts with no roof events yet

- **Decision**:
  - Keep AlertEngine's generic parts: `Alert`, levels, `renderTemplate`, `stackAlerts`, the
    `Tracker` edge/cooldown mechanism, `AlertSchedule`, and the send mode with pause/resume.
  - `AlertType` becomes `Test` plus the imaging-app (client) events and `AlertsResumed`. With no
    Alpaca device the client events are reported inactive through SettingsDeps ("No Alpaca device
    yet"), not hidden.
  - The `AlertInputs` and `AlertRules` for safety, rain, sensors, dew, sky and battery go, along
    with the battery NVS blob.
- **Rationale**: SQMeter's spec 021 imaging-app alerts ("N.I.N.A. stopped polling") matter for a
  roof. Keeping them saves re-porting them in 007.

## R5. Configuration model

- **Decision**: These sections survive:
  - `deviceName`, `wifi`, `ntp`, `timeZone`, `ota`, `auth`;
  - `mqtt`, with publish flags reduced to `diagnostics`;
  - `alpaca.enabled`;
  - `alerts`: channels, cooldown, send mode, client-silence, and the event settings for the
    remaining events.

  NVS namespace `roofroof`, blob keys as in SQMeter minus `alertsBattB`, and SQMeter's legacy
  string keys dropped (nothing to migrate from). The size limits stay (5100 / 3900 bytes).
- **Migration fixtures**:
  - `test/fixtures/config-releases/` starts empty; `test_config_migration` passes when the
    directory holds no files.
  - The `v0.1.0` tag adds the first fixture through `tools/config-releases/build.sh`, adapted to
    RoofRoof's paths.
  - The ranges fixture keeps only `alerts`, `alpaca`, `mqtt`, `ntp` and `wifi` entries.
- **Alternatives**: keep SQMeter's legacy keys "in case". Rejected: no RoofRoof device ever wrote
  them (edge case: an SQMeter board's old namespace is simply ignored).

## R6. Firmware image marker: product field and new magic

- **Decision**:
  - New 16-byte magic.
  - The marker string becomes `product=roofroof;layout=l2;build=standard;` (42 bytes, under the
    48-character field limit).
  - `FirmwareImage` parses `product`, and `check()` compares product first.
  - `verdictMessage` gains "This is firmware for another product (sqmeter)".
- **Rationale**: A new magic alone would refuse SQMeter images as "unknown" rather than "wrong
  product". The product field gives a precise message, and it is cheap to upstream (heritage:
  upstream candidate).
- **Alternatives**: magic only. Rejected because the refusal message would be vaguer (spec 001
  US4-AC2).

## R7. Holding the driver off

- **Decision**:
  - New pure library `lib/DriverLogic`: `enum class DriverCommand { Off, Forward, Reverse }` plus
    a duty value, and `DriverOutputs outputsFor(DriverCommand, uint8_t duty)`, which returns the
    EN, RPWM and LPWM levels.
  - Native tests prove that RPWM and LPWM are never both non-zero, and that `Off` gives EN low with
    both PWM at 0.
  - `src/MotorDriver.cpp` holds the only code that writes those pins. `MotorDriver::holdOff()` is
    the first statement of `setup()`, before Serial.
  - In 001 nothing calls anything but `Off`; spec 002/003 add the motion task.
  - `include/RoofPins.h` holds the pin table.
- **Rationale**: Constitution I says one module sets the outputs. Starting it now means 002 adds to
  it instead of introducing it.
- **Boot gap**: from reset until `setup()` (bootloader plus app start, about 300 ms) GPIO25-27
  float. That is why the constitution requires a pull-down on EN. The hardware docs give 10 kΩ from
  GPIO27/EN to GND. An optional pull-down on RPWM/LPWM is documented as belt and braces.
- **Strapping**: none of GPIO25/26/27 are strapping pins on the ESP32, so pull-downs there don't
  affect boot.

## R8. Updates without GitHub Releases

- **Decision**:
  - Remove OtaUpdater, ReleaseLogic, ReleaseUrls, the `/api/updates/check|apply` routes, the
    release-track setting and the Updates page's release list.
  - Move `httpsDownload` nowhere; its only other user, LanguagePack, also goes.
  - Remove `GITHUB_ROOT_CA_PEM` from `RootCertificates.cpp` (the Pushover set keeps the roots it
    needs).
  - The "network busy" check that read the OtaUpdater phase reads the upload state instead.
  - Keep manual upload (src/WebServerUpdates.cpp upload path) and ArduinoOTA (opt-in) unchanged.
- **Release workflow**: on a `v*` tag, build `roofroof-l2-firmware-<tag>.bin`,
  `roofroof-l2-littlefs-<tag>.bin`, the USB package and the OpenAPI JSON, and attach them to a
  GitHub release on the private repo.

## R9. Docs and demo without Pages

- **Decision**:
  - `docs.yml` keeps the demo build, screenshots, a11y, mermaid/scalar vendoring, the diagram
    checks and `mkdocs build --strict`.
  - Removed: the Pages deploy, the external demo repo push, `flash_assets.py` (it reads public
    releases) and the esp-web-tools browser flasher.
  - The built `site/` (with the demo under `site/demo/`) is uploaded as an artifact.
  - `docs.yml` runs on pull requests, not only on `main`, so the docs build is a merge gate
    (constitution IX).
  - `site_url` is left empty; links are relative.

## R10. English only

- **Decision**:
  - `languages.ts` lists only `en`.
  - Remove `locales/`, `loader.ts`, `build_packs.py`, `translate.py`, `record.json`, `review/`,
    the 13 glossaries and the language-specific Playwright specs (`i18n.spec`, `language-progress`).
  - `api-locale` and `locale-numbers` stay, run against a test-only `de` pack built in the test.
    I18N-06 (machine formats never localised) still needs proving.
  - `literals.mjs`, `check.mjs`, `gen_device_catalog.py` and `context.py` keep running; their
    hard-coded source lists are trimmed to files that exist.
- **Rationale**: Keeps I18N-01..06 enforceable, so a language is one file later.

## R11. Time without GPS

- **Decision**: TimeManager loses `GPSSensor*`, `GPSConfig` and `TimeSource::Gps`, leaving NTP
  only. The time-source priority setting is removed. Location goes too: it existed for Sun & Moon
  and darkness, and spec 005 can add it back if the roof ever needs sunrise times.

## R12. Toolchain

- PlatformIO Core 6.2.0, pioarduino `55.03.312-1` (Arduino-ESP32 3.3.12 / IDF 5.5.5).
- One env, `esp32dev`, plus `native`. `lib_deps` drop the Adafruit sensor libraries and
  TinyGPSPlus.
- Node 24 in CI (Node 26 locally works).
- Emscripten 6.0.12 locally: `tools/demo-core/VERSION` is updated when the core is rebuilt.
- Python 3.12 in CI with SQMeter's pinned quality tools (clang-format 19.1.7, clang-tidy 19.1.0,
  lizard 1.17.13, ruff 0.7.4), and mkdocs-material 9.7.7.

## R13. Licence and notices

SQMeter is MIT © 2026 Dean0, the same owner. RoofRoof keeps `LICENSE` (MIT),
`THIRD_PARTY_NOTICES.md` and `licenses/OFL-1.1.txt` (Inter and JetBrains Mono are bundled). The
fonts keep their files; the `SQM` suffix in the font-family names becomes `RR`.
