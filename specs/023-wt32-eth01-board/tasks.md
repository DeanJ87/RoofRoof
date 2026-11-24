# Tasks: Board Presets, Pin Setup and WT32-ETH01 Ethernet

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/pins-api.md](contracts/pins-api.md)

## Phase 1: Setup

- [x] T001 Roadmap entry for 023 in specs/ROADMAP.md (scope, base, out of scope) and the hardware facts (two boards, no rain contact)
- [x] T002 `lib/BoardLogic` skeleton; `-Ilib/BoardLogic/include` in the native env; demo-core sources

## Phase 2: Pure logic, tests first (US1, US2, US3, US4, US5)

- [x] T003 [US1] Native tests `test/test_board_pins`: both presets pass with only their expected warnings; one test per error code (missing, flash, psram, uart, ethernet, header, inputOnly, notAdc1, noPullup, shared, enStrap, presetMismatch) and per strap warning; the boot decision (valid, invalid, unreadable) and its hold-as-input list (never a reserved pin)
- [x] T004 [US1] `PinMap.h`, `PinRules.h`, src/PinMap.cpp, src/PinRules.cpp, src/PinText.cpp in lib/BoardLogic
- [x] T005 [US3] Native tests `test/test_wiring_check`: every verdict and boundary (EN low/high/not run; Hall rails and range; supply 3 V, limit, 15 V; park open/closed/unreadable/not fitted; current near zero, high, off in mV; Ethernet found/link/no PHY/probe found/other PHY/skipped; the motion item)
- [x] T006 [US3] `WiringCheck.h`, src/WiringCheck.cpp in lib/BoardLogic
- [x] T007 [US5] Native tests `test/test_mdio`: the read frame's 46 bits, decode with a good and a bad turnaround, identification of LAN8720A/other/none, the clock test
- [x] T008 [US5] `Mdio.h`, src/Mdio.cpp in lib/BoardLogic
- [x] T009 [US4] Native tests `test/test_network_plan`: DevKit (station at once, hotspot by the existing rule), Ethernet board fallback (station after 30 s down, never before, stays), off (never), hotspot at 45 s without any join, never after a join
- [x] T010 [US4] `NetworkPlan.h`, src/NetworkPlan.cpp in lib/BoardLogic
- [x] T011 [US1] Tests in test/test_config_model: `pins` default, an old document loads as DevKit, a preset alone fills its pins, a mismatching named preset refused, a Custom set checked by the rules, JSON round trip, `network.mode` default and values, `safety.rainContact` ignored, the stored size with the new group
- [x] T012 [US1] ConfigModel: `PinMap pins`, `NetworkConfig::mode`; JSON in/out; validate; `Config::load` reports a rejected store; rain contact removed
- [x] T013 [US1] ConfigBackup restart reason `pins` (and `network.mode` with wifi); test/fixtures/backup/restart-cases.json
- [x] T014 [US6] Rain contact out of lib/SafetyInputLogic (RainContact, the reason bit, InputsReport, SafetySettings) and lib/RoofIoLogic (SwitchInput, RoofStatus); tests updated
- [x] T015 [US2] `FaultCause::PinSetup` (encode, decode, text) and test
- [x] T016 Demo core rebuilt (`tools/demo-core/build.sh`)

## Phase 3: Firmware

- [x] T017 [US2] `PinSetup` (boot decision with the chip's PSRAM, the pins in effect, holding candidates); main's boot order: serial, settings, pins, then the rest
- [x] T018 [US2] `MotorDriver::configure/disable` and the EN probe; `RoofPins.h` removed; RoofControl/RoofControlTask/RoofBench read the pins in effect; RoofControl doesn't start with invalid pins and refuses commands with the reason
- [x] T019 [US4] `EthernetLink` (start on the WT32 board, events, status, IPv6 addresses, default route) and `NetState::online()`; WiFiManager follows `Net::wifiPlan`; every former Wi-Fi check (mDNS, OTA, NTP, poller, alerts, deps facts, platform guard, IPv6 peers) uses `online()`; hotspot restart on Ethernet
- [x] T020 [US3] Wiring check runner on the main loop (`WiringCheck`), the MDIO/clock probe, results
- [x] T021 [US1] Routes: `GET /api/pins`, `POST /api/pins/validate`, `POST`/`GET /api/pins/check`; the config save refuses a pins or Wi-Fi-role change while busy and checks the chip; status `ethernet` and `pins`, `roof.rainContact` gone; status capacity
- [x] T022 [US6] Firmware rain contact removal (RoofControl, SafetyInputs, main)

## Phase 4: Web and demo

- [x] T023 [US1] Types, zod, defaults, mocks for `pins`, `network.mode`, status blocks; rain contact out of types, zod, mocks
- [x] T024 [US1] `lib/pinsApi.ts` with Vitest
- [x] T025 [US1] Pins page (`/pins`): board and preset cards, pin fields, issues, Save and Restart; links from Settings → Roof and System; Vitest
- [x] T026 [US3] Wiring check panel (shared by the page and the wizard); Vitest
- [x] T027 [US1] Wizard: board screen and Check wiring screen on the Hardware page; rain screen removed; review; Vitest
- [x] T028 [US4] Settings → Network: Ethernet card (status, Wi-Fi role); status rows on the dashboard and System page; restart reasons
- [x] T029 [US6] Rain contact UI removed (Settings → Roof, dashboard, wizard, demo panel)
- [x] T030 i18n (en.json, en.context.json, device catalogue), Playwright inventory and screenshots
- [x] T031 Demo: bridge `pins(action, body)`; handlers; simulated wiring check

## Phase 5: Contracts, docs, governance

- [x] T032 Constitution 4.0.0 with its Sync Impact Report
- [x] T033 `tools/api/routes.json`, `operations.json`, `docs/api/openapi.json`, contract schemas, docs/api/rest.md and websocket.md
- [x] T034 Docs: hardware/wiring.md (both maps, carrier notes, straps), user-guide pins page, network (Ethernet), configuration, safety-inputs, commissioning and checklist; heritage; CHANGELOG
- [x] T035 Settings-deps catalogue and `tools/settings-deps/check.py`, `tools/dashboard/check.py`

## Phase 6: Verify

- [x] T036 Quickstart §1 of spec 001: firmware build, native tests, web typecheck/test/build, Playwright, quality gate, flash and UI budgets, docs build, source hash

## Phase 7: Owner requirements added during implementation (converge)

- [x] T037 [US5] `Preset::Unset` for a new device; the boot probe (src/EthernetProbe.cpp) before any pin is touched; the board in effect from the probe; `GET /api/pins` `detected`; settings saved before spec 023 migrated to the DevKit explicitly; tests in test_board_pins, test_config_pins
- [x] T038 [US4] `network.mode` (auto, ethernet, wifi) replacing the Wi-Fi role; `Net::wifiPlan` with the link's 20 s DHCP wait and no hotspot after a join or in Ethernet only; Wi-Fi scan and join refused in Ethernet only; tests in test_network_plan
- [x] T039 Docs: first flash of a WT32-ETH01; network page; spec, research and constitution updated

## Phase 8: Review rounds

- [x] T040 Round 1 (firmware): the probe's pins reserved on every board; MDIO sampled after the falling edge with one turnaround bit; an Alpaca stop with invalid pins answered OK; the default route follows the Wi-Fi fallback; a board whose LAN8720 answered refuses a non-WT32 map; GPIO 37/38 refused; a stored unknown network mode loads as Auto; Ethernet only as in effect; a busy EN check says so; a backup never restores the pins; the pin fault names its reason
- [x] T041 Round 2: GPIO16/17 reserved on every board; `boardDetected` in validation and saves (a WT32 in WiFi only probed at boot); the Ethernet check skipped in WiFi only and when busy; `ethernet.mode` in the status; the web UI's empty-SSID rule, the detected-WT32 DevKit card, Clear fault for the pin fault, restart refusals, neutral WiFi beside Ethernet, live regions; settings saves merged on the heap with the async_tcp low-water logged
- [x] T042 Round 3: the WiFi-only Settings note, the wiring check refused when the controller didn't start, Ethernet before PSRAM in the rules, wording (reviewer: safe to merge)

## Phase 9: Owner requirement: the analog front end (Advanced)

- [x] T043 [US1] `lib/BoardLogic` Analog (dividers, trims, presets, checks, scaling, supply calibration) with test_analog; `analog` settings and the park pull-ups in ConfigModel with tests in test_config_pins
- [x] T044 Firmware: the roof scales the supply and current with the configured values (applied with the roof's settings, never mid-move); the park pull-ups at boot; the pin documents and validate carry the analog values and issues; the demo too
- [x] T045 Web: the Advanced section, the supply calibration, mocks, tests; docs

## Phase 10: Review fixes after the merge (clarification 2026-11-23)

- [x] T046 [US1] Native tests first (test_analog, test_config_pins, test_config_backup, test_roof_gates, test_config_model): trim ranges, gate reach, broken-wire floor, supply raise, preset tolerance, NaN for an unusable divider, calibration from the saved setup, non-object inputs, Custom and WT32 round trips, analog as a motion setting, analog never restored
- [x] T047 `lib/BoardLogic` Analog: ranges 0.8-1.25 / ±1 V / ±0.5 A, `AnalogGates` and the gate checks, `highestSenseAmps`, the preset compared within 0.0001, NaN for ratio 0, the resistor sum in double, `calibratedSupplyGain(saved, edited, …)`; static_assert that a valid current offset reads as readable (RoofSettings.cpp)
- [x] T048 ConfigModel: `Config::analogGates()`, `validateAnalog` against the roof settings, non-object inputs refused; pin documents and validate checked against the gates (firmware and demo); `motionSettingsChanged` counts `analog`
- [x] T049 ConfigBackup: `analog` never restored, listed as `board-analog`
- [x] T050 Web: the calibration from the saved setup, the analog preset no longer follows the board (Pins page, wizard, wiring suggestion), zod ranges, the backup reason; tests
- [x] T051 Docs (pins, configuration, REST), constitution 4.1.1 (rain-contact wording), CHANGELOG
- [x] T052 Review round 1: the offset weighed by 100 / slow duty (broken-wire floor, −0.5 A), the reach capped by the IS output's 5 V and the ADC taken as 2.9 V, stored analog values that fail the checks refuse every move (`Roof::Settings::analogUsable`), the backup reason enum in the contract and OpenAPI, the wiring check's supply text
- [x] T053 Review round 2: the driven offset uses the duty as driven (255 / step), the reach takes the worse offset at the slow duty, the demo core applies `analogUsable` too, the settings-invalid refusal names the Pins page
