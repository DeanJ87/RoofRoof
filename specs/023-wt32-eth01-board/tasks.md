# Tasks: Board Presets, Pin Setup and WT32-ETH01 Ethernet

**Input**: [spec.md](spec.md), [plan.md](plan.md), [research.md](research.md),
[data-model.md](data-model.md), [contracts/pins-api.md](contracts/pins-api.md)

## Phase 1: Setup

- [ ] T001 Roadmap entry for 023 in specs/ROADMAP.md (scope, base, out of scope) and the hardware facts (two boards, no rain contact)
- [ ] T002 `lib/BoardLogic` skeleton; `-Ilib/BoardLogic/include` in the native env; demo-core sources

## Phase 2: Pure logic, tests first (US1, US2, US3, US4, US5)

- [ ] T003 [US1] Native tests `test/test_board_pins`: both presets pass with only their expected warnings; one test per error code (missing, flash, psram, uart, ethernet, header, inputOnly, notAdc1, noPullup, shared, enStrap, presetMismatch) and per strap warning; the boot decision (valid, invalid, unreadable) and its hold-as-input list (never a reserved pin)
- [ ] T004 [US1] `PinMap.h`, `PinRules.h`, src/PinMap.cpp, src/PinRules.cpp, src/PinText.cpp in lib/BoardLogic
- [ ] T005 [US3] Native tests `test/test_wiring_check`: every verdict and boundary (EN low/high/not run; Hall rails and range; supply 3 V, limit, 15 V; park open/closed/unreadable/not fitted; current near zero, high, off in mV; Ethernet found/link/no PHY/probe found/other PHY/skipped; the motion item)
- [ ] T006 [US3] `WiringCheck.h`, src/WiringCheck.cpp in lib/BoardLogic
- [ ] T007 [US5] Native tests `test/test_mdio`: the read frame's 46 bits, decode with a good and a bad turnaround, identification of LAN8720A/other/none, the clock test
- [ ] T008 [US5] `Mdio.h`, src/Mdio.cpp in lib/BoardLogic
- [ ] T009 [US4] Native tests `test/test_network_plan`: DevKit (station at once, hotspot by the existing rule), Ethernet board fallback (station after 30 s down, never before, stays), off (never), hotspot at 45 s without any join, never after a join
- [ ] T010 [US4] `NetworkPlan.h`, src/NetworkPlan.cpp in lib/BoardLogic
- [ ] T011 [US1] Tests in test/test_config_model: `pins` default, an old document loads as DevKit, a preset alone fills its pins, a mismatching named preset refused, a Custom set checked by the rules, JSON round trip, `wifi.withEthernet` default and values, `safety.rainContact` ignored, the stored size with the new group
- [ ] T012 [US1] ConfigModel: `PinMap pins`, `WiFiConfig::withEthernet`; JSON in/out; validate; `Config::load` reports a rejected store; rain contact removed
- [ ] T013 [US1] ConfigBackup restart reason `pins` (and `wifi.withEthernet` with wifi); test/fixtures/backup/restart-cases.json
- [ ] T014 [US6] Rain contact out of lib/SafetyInputLogic (RainContact, the reason bit, InputsReport, SafetySettings) and lib/RoofIoLogic (SwitchInput, RoofStatus); tests updated
- [ ] T015 [US2] `FaultCause::PinSetup` (encode, decode, text) and test
- [ ] T016 Demo core rebuilt (`tools/demo-core/build.sh`)

## Phase 3: Firmware

- [ ] T017 [US2] `PinSetup` (boot decision with the chip's PSRAM, the pins in effect, holding candidates); main's boot order: serial, settings, pins, then the rest
- [ ] T018 [US2] `MotorDriver::configure/disable` and the EN probe; `RoofPins.h` removed; RoofControl/RoofControlTask/RoofBench read the pins in effect; RoofControl doesn't start with invalid pins and refuses commands with the reason
- [ ] T019 [US4] `EthernetLink` (start on the WT32 board, events, status, IPv6 addresses, default route) and `NetState::online()`; WiFiManager follows `Net::wifiPlan`; every former Wi-Fi check (mDNS, OTA, NTP, poller, alerts, deps facts, platform guard, IPv6 peers) uses `online()`; hotspot restart on Ethernet
- [ ] T020 [US3] Wiring check runner on the main loop (`WiringCheck`), the MDIO/clock probe, results
- [ ] T021 [US1] Routes: `GET /api/pins`, `POST /api/pins/validate`, `POST`/`GET /api/pins/check`; the config save refuses a pins or Wi-Fi-role change while busy and checks the chip; status `ethernet` and `pins`, `roof.rainContact` gone; status capacity
- [ ] T022 [US6] Firmware rain contact removal (RoofControl, SafetyInputs, main)

## Phase 4: Web and demo

- [ ] T023 [US1] Types, zod, defaults, mocks for `pins`, `wifi.withEthernet`, status blocks; rain contact out of types, zod, mocks
- [ ] T024 [US1] `lib/pinsApi.ts` with Vitest
- [ ] T025 [US1] Pins page (`/pins`): board and preset cards, pin fields, issues, Save and Restart; links from Settings → Roof and System; Vitest
- [ ] T026 [US3] Wiring check panel (shared by the page and the wizard); Vitest
- [ ] T027 [US1] Wizard: board screen and Check wiring screen on the Hardware page; rain screen removed; review; Vitest
- [ ] T028 [US4] Settings → Network: Ethernet card (status, Wi-Fi role); status rows on the dashboard and System page; restart reasons
- [ ] T029 [US6] Rain contact UI removed (Settings → Roof, dashboard, wizard, demo panel)
- [ ] T030 i18n (en.json, en.context.json, device catalogue), Playwright inventory and screenshots
- [ ] T031 Demo: bridge `pins(action, body)`; handlers; simulated wiring check

## Phase 5: Contracts, docs, governance

- [ ] T032 Constitution 3.0.0 with its Sync Impact Report
- [ ] T033 `tools/api/routes.json`, `operations.json`, `docs/api/openapi.json`, contract schemas, docs/api/rest.md and websocket.md
- [ ] T034 Docs: hardware/wiring.md (both maps, carrier notes, straps), user-guide pins page, network (Ethernet), configuration, safety-inputs, commissioning and checklist; heritage; CHANGELOG
- [ ] T035 Settings-deps catalogue and `tools/settings-deps/check.py`, `tools/dashboard/check.py`

## Phase 6: Verify

- [ ] T036 Quickstart §1 of spec 001: firmware build, native tests, web typecheck/test/build, Playwright, quality gate, flash and UI budgets, docs build, source hash
