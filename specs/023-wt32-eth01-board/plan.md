# Implementation Plan: Board Presets, Pin Setup and WT32-ETH01 Ethernet

**Branch**: `spec/023-wt32-eth01-board` | **Date**: 2026-11-23 | **Spec**: [spec.md](spec.md)

## Summary

- New `lib/BoardLogic` (pure, native-tested):
  - `PinMap.h`: the board and preset enums, `PinMap`, the two presets, `Facts` per board and chip;
  - `PinRules`: `check(map, facts)` → errors and warnings with codes and texts; `bootPins` (the
    boot decision and the pins to hold as inputs);
  - `WiringCheck`: the verdict for each wiring check from plain readings;
  - `Mdio`: clause-22 read frames, the read decode, the PHY identification, the clock test;
  - `NetworkPlan`: whether to start the Wi-Fi station and open the setup hotspot.
- `lib/ConfigModel`: the `pins` group (JSON in/out, defaults, structural validation and the rules
  with a chip without PSRAM), `network.mode`, `safety.rainContact` removed (ignored on read);
  `Config::load` says when stored settings were present but rejected.
- `lib/ConfigBackup`: the `pins` restart reason; the restart-cases fixture.
- `lib/SafetyInputLogic`, `lib/RoofIoLogic`: the rain contact removed (verdict input, reason bit 8
  left unused, reports, status); `FaultCause::PinSetup` and its text.
- Firmware:
  - `src/PinSetup.cpp` (new): the boot decision, the pins in effect, holding candidates as inputs;
  - `MotorDriver`: pins set at runtime (`configure`), or none (`disable`); the EN pull-down probe;
  - `RoofControl`: reads the pins in effect; doesn't start with an invalid set (fault latched,
    every command refused with the reason); `probeEnable()` under the lock;
  - `src/WiringCheck.cpp` (new): runs on the main loop: the EN probe, the snapshot's readings, the
    Ethernet probe or status, then the pure verdicts;
  - `src/EthernetLink.cpp` (new): `ETH` start, events, status, IPv6 addresses;
  - `src/NetState.cpp` (new): `NetState::online()` (Wi-Fi joined or Ethernet addressed), used by
    every former Wi-Fi check; WiFiManager follows the plan;
  - main: boot order (settings, pins, then the rest), NTP and command-line OTA once online;
  - WebServer: `GET /api/pins`, `POST /api/pins/validate`, `POST`/`GET /api/pins/check`; the
    config save's busy refusal for pins and the chip check; status `ethernet` and `pins`.
- Web: types and zod for `pins` and `network.mode`; `lib/pinsApi.ts`; a Pins page
  (`/pins`) with the board and preset choice cards, the pin fields, the issues, Save and Restart,
  and the wiring check; the wizard's board and Check wiring screens; Settings → Network's Ethernet
  card; status rows; rain contact UI removed; i18n; MSW mocks; Vitest; Playwright inventory.
- Demo: the bridge answers the pin routes from the WASM core (rules and presets from lib); the
  wiring check returns the simulated roof's readings.
- Constitution 3.0.0, roadmap, docs, OpenAPI, contracts, CHANGELOG.

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5); TypeScript strict |
| **Primary Dependencies** | Arduino `ETH` (LAN8720 over the ESP32 EMAC); spec 003's MotorDriver and control task; spec 010's platform guard; spec 015/018's wizard; spec 021's partial saves |
| **Storage** | the `pins` group and `network.mode` in the settings document (about 170 bytes more) |
| **Testing** | new `test_board_pins`, `test_wiring_check`, `test_mdio`, `test_network_plan`; `test_config_model`, `test_safety_input_logic`, `test_roof_*` updated; Vitest for the page, the wizard screens and the API; Playwright inventory |
| **Target Platform** | ESP32 DevKit (WROOM) and WT32-ETH01, one image |
| **Constraints** | flash 86.3 % before: Ethernet and this feature must stay under 90 % |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | EN low before anything but the settings read; invalid pins drive nothing and the control task doesn't start; MotorDriver still the only writer; pin changes only at boot and refused while busy; the EN probe only with both PWM low, under the lock and EN's critical section | `test_board_pins` (every rule, the boot decision), review of `PinSetup`/`MotorDriver` |
| II. Parked mount | Unchanged; the park inputs read the pins in effect | `test_roof_gates` unchanged |
| III. Unsafe ends closed | The local contact is removed (MAJOR 3.0.0, risk stated); SQMeter's rain and fault bits unchanged | `test_safety_input_logic` |
| IV. Alpaca | Unchanged; discovery over every interface | ConformU job |
| V. Pure logic | Rules, boot decision, verdicts, MDIO, Wi-Fi plan in `lib/BoardLogic`, tests first | native suites |
| VI. Budgets | Ethernet within the warning line (Flash below); the wiring check on the main loop; the status capacity re-measured | build, `flash_budget.py`, `test_status_document_fits` |
| VII. UI | Choice cards (DS-12), existing cards and fields; Stop bar unchanged; i18n | Vitest, Playwright |
| VIII. Security | Wiring check and settings save behind `requireAuth`; validate and `GET /api/pins` reveal no secret | review |
| IX. Docs | wiring (both maps), network, pins page, configuration, REST/OpenAPI, commissioning, safety inputs | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py limits | check.py |
| XI. Heritage | SQMeter's `BoardProfile` idea reused (recorded); WiFiManager diverges (Ethernet) | heritage.md |

**Gate**: pass. Principle III is relaxed by removing a weather input: a MAJOR amendment (3.0.0)
states the new risk (spec, assumption 13). Principle I's boot rule is reworded, not relaxed: the
driver's off state through the settings read rests on the same pull-down it rests on through reset.

## Flash

Before: 1,583,296 B (86.3 %). After: 1,655,312 B (90.2 %, over the 90 % warning, below the 95 % failure line, after the analog settings),
recorded in `tools/firmware/size-baseline.json`. The largest part is ESP-IDF's EMAC and LAN87xx
driver (about 40 KB); Arduino's `ETH` class would have added about 16 KB more (research R1). The
next spec that adds flash should look for savings first.

## Project Structure

```text
specs/023-wt32-eth01-board/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
.specify/memory/constitution.md (3.0.0), specs/ROADMAP.md, CHANGELOG.md
lib/BoardLogic/ (new) include/{PinMap,PinRules,WiringCheck,Mdio,NetworkPlan}.h, src/*.cpp
lib/ConfigModel, lib/ConfigBackup, lib/SafetyInputLogic, lib/RoofIoLogic
include/{PinSetup,WiringCheck,EthernetLink,NetState}.h (new); include/RoofPins.h (removed)
src/{PinSetup,WiringCheck,EthernetLink,NetState,WebServerPins}.cpp (new)
src/{main,MotorDriver,RoofControl,RoofControlTask,RoofBench,WiFiManager,SafetyPoller,AlertDispatcher,PlatformGuard,Ipv6Network,WebServer*,SafetyInputs}.cpp
test/test_board_pins, test_wiring_check, test_mdio, test_network_plan (new)
tools/demo-core (bridge, emulated device), tools/api, tools/contracts, tools/i18n
web/src: components/Pins.tsx, components/pins/*, wizard screens, settings/NetworkTab, types, validation, lib/pinsApi.ts, mocks, i18n
docs/hardware/{wiring,commissioning,commissioning-checklist}.md, docs/user-guide/{pins,network,configuration,safety-inputs}.md, docs/api/{rest.md,openapi.json}
```

**Structure Decision**: every rule about pins is one pure function the device runs at boot and at
save, and the UI asks the device (or the demo's WASM copy) rather than repeating it in TypeScript,
so the two can't drift. The hardware layer only applies what the rules allowed.

## Phases

1. **Pure logic, tests first**: BoardLogic (rules, boot decision, verdicts, MDIO, Wi-Fi plan);
   ConfigModel pins group and the rain contact's removal; FaultCause.
2. **Firmware**: boot order and PinSetup; MotorDriver and RoofControl on runtime pins; Ethernet and
   NetState; the wiring check; routes; status.
3. **Web and demo**: API, page, wizard, Network tab, rain UI removed, mocks, demo bridge.
4. **Constitution, roadmap, docs, OpenAPI, contracts**.
5. **Verify**: quickstart §1 of spec 001, converge, independent reviews until safe to merge.

## Complexity Tracking

None.
