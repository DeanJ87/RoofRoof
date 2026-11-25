# Implementation Plan: N.I.N.A. Park Before Every Move, and Guards a Person Can Confirm

**Branch**: `spec/027-nina-park-and-open-guards` | **Spec**: [spec.md](spec.md)

## Summary

The gates in `lib/RoofIoLogic` gain the park check for Open, an "already there" success before every
check, and override bits a person may set. A pure `ParkWait` in `lib/RoofIoLogic` turns an Open or
Close refused only for the park into a wait that asks N.I.N.A. to park, holds the command, and runs
it once the park holds; `RoofControl` (and the demo's roof) own it under the roof's lock, the main
loop sends the park request and the alert. An override in effect (`RoofIo::Override`) is kept by the
roof and read by the automatic close. The Hall rail fault clears itself at rest after 60 s. N.I.N.A.
gains https, `POST /api/nina/test` and the `nina_lost` alert; the status gains `parkWait`,
`override`, `openOverridable`, `nina` and `sqmeter`. Alpaca names the device "RoofRoof" and reports
the wait. The dashboard's roof card shows a dialog listing every reason with the override; Settings'
N.I.N.A. card gets its Test. Home Assistant gains ten entities and its documentation.

## Technical Context

C++17 firmware (lib pure, Unity native tests), Preact + TypeScript UI (Vitest, Playwright against
the WASM demo core), as in spec 001. Budgets before the work: flash 92.8% of the OTA slot (fail at
95%), LittleFS at its 75% limit, `Config.h` at its 600-line limit (research R5).

## Constitution Check

| Principle | How it is kept |
|---|---|
| I Motion Safety | Hall rail at rest clears after 60 s of good readings at rest (7.0.0 states it and the risk); every other fault unchanged. A wait never drives; the command it runs goes through every check. Native tests for both latching cases and the boundary. |
| II Parked mount | Open needs the park (more protective) and its re-check runs during an open. Waits from every origin. The person's confirmed close is the 7.0.0 exception: person origin only, named in the request; tests prove Alpaca and MQTT can't. |
| III Unsafe ends closed | The person's confirmed open: daylight and SQMeter-unsafe-not-rain only; rain and doubt never; the override ends with its reasons; the automatic close stands back only for overridden reasons. Tests on each end. Unreachable N.I.N.A. still means not parked for the automatic close. |
| IV Alpaca | Open and Close return at once; a wait reports opening/closing and Slewing; a failed wait reads error; already-there is success. ConformU in CI against the simulator. Name and driver info changed; UniqueID unchanged. |
| V Testable logic | ParkWait, the gates with overrides, the override's end, Hall clear, the N.I.N.A. test parse and the alert condition in lib with native tests; UI in pure modules with Vitest; Playwright for the dialog. |
| VI Budgets | Lean additions; flash measured and recorded; UI strings kept short; LittleFS within 75% (R5); Config.h trimmed by one comment line for the new alert event. The N.I.N.A. test runs on the site job's task, never on the async TCP task; one TLS session (TlsLock). |
| VII UI | Modal dialog with the Stop bar inside; danger button for the overrides; every greyed or refused button explains; no new styles. |
| VIII Security | The override and the N.I.N.A. test need the password and the command header; Alpaca and MQTT have no override. |
| IX Docs | User guide, REST, MQTT, Home Assistant (new page), configuration, Alpaca, diagrams re-confirmed. |
| X Quality | Quality gate run. |

Constitution 7.0.0 (MAJOR) records the three relaxations and the risks.

## Project Structure

- lib/RoofIoLogic: `RoofGates` (park for Open, override bits, weather reasons), `CommandCheck`
  (already-there first, overrides), new `ParkWait`, `Override`, `RoofStatus` fields, `DomeBridge`.
- lib/RoofLogic: `Controller::atEnd`, Hall rail at-rest clear.
- lib/AlpacaLogic: Dome name, driver info, description.
- lib/AlertLogic, lib/DeviceCore, lib/ConfigModel: `nina_lost` event.
- lib/ConfigModel: https for N.I.N.A.
- lib/NinaLogic: test answer document.
- lib/HaDiscovery: new entities; Alerts switch always.
- lib/CloseSequenceLogic: the override masks the sequence's inputs.
- src: RoofControl (wait, override), main (park request, alert), CloseOnUnsafe, WebServerRoof
  (override body), WebServerSite (N.I.N.A. test), WebServerAlpaca, status.
- tools/demo-core: the same wait and override in the demo roof; N.I.N.A. test.
- web: RoofCard buttons and `MoveDialog`, `moveBlockers.ts` (overridable), ParkFields (Test),
  SQMeter and N.I.N.A. rows, Diagnostics sources, schemas, i18n.
- scripts: `version.py` (commit in the version).
- docs: integration/home-assistant.md and the user guide pages.

## Pull requests

One PR (spec 027). The parts share the roof card, the status document and the budgets.

## Complexity Tracking

None outside the constitution amendment.
