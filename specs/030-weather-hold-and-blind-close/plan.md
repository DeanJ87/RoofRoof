# Implementation Plan: Weather Hold, Closing Without a Known Position, and Never Sitting Silent

**Branch**: `spec/030-weather-hold-and-blind-close` | **Spec**: [spec.md](spec.md)

## Summary

A pure `WeatherHold` in `lib/CloseSequenceLogic` sits between the verdict and the automatic close:
it adds up unsafe time and lets the close start once it reaches `autoClose.unsafeHoldSeconds`
(rain at once), and stays fired until the weather has been safe as long. The roof controller gains
two automatic-only ways to close without a known position: from an unknown position with a working
Hall sensor (the person's slow move to the first magnet), and a blind close on time with the Hall
sensor failed, stopped by a supply-sag stall check. The close sequence runs both through its usual
park request and wait. An owner option lets the automatic close go ahead without N.I.N.A.'s park
after minutes of N.I.N.A. silence. A `needs_person` critical alert covers restarts, Hall faults, a
sequence that can't move and the closes above; every alert that fails is retried, and critical ones
are kept in NVS over a restart. SQMeter's reasons are read as text and shown as words. Flash is paid
for with link-time optimisation of the project's own code.

## Technical Context

C++17 firmware (lib pure, Unity native tests), Preact + TypeScript UI, as in spec 001. Budgets
before the work (main 448f16b): flash 1,728,496 B = 94.2% of the 1,835,008 B OTA slot (fail at
95%); LittleFS at its 75% limit; `Config.h` at 600 lines (research R4, R5).

## Constitution Check

| Principle | How it is kept |
|---|---|
| I Motion Safety | The two moves without a known position are the 8.0.0 exceptions: automatic close only, towards closed only, slow duty, hard limit (the unknown-position timeout), every fault check, Stop; blind close once per fault, kept in the fault record; the stall stop from the supply sag; risk stated. Native tests for each end. |
| II Parked mount | Every close path (normal, unknown position, blind) goes through the gates and the park wait. Default: never close without the park, alert reminded. The N.I.N.A.-lost close is the owner option (off by default), automatic origin only, switches still count; tests prove Alpaca, MQTT and a person can't use it. |
| III Unsafe ends closed | The hold delays the close's start only, rain never; the verdict and Open's refusal are immediate; the hold is bounded (300 s); risk stated in 8.0.0. |
| IV Alpaca | OpenShutter while unsafe returns the Alpaca error naming the reason (tests); blind and unknown-position closes report closing. ConformU in CI. |
| V Testable logic | WeatherHold, the controller's moves, the stall check, the N.I.N.A.-lost timer, the needs-person watcher, the alert queue and its NVS record, the SQMeter reasons parser: all in lib with native tests, including the field sequences (W05, the access point outage). |
| VI Budgets | LTO for src/ and lib/ (about -110 KB); the NVS record is written only when a critical alert is queued or delivered; Config.h split (`ConfigAutoClose.h`); UI additions small. |
| VII UI | Banner (DS-14) for "Roof needs a person"; Notes for the hold and the closes; settings with existing controls. |
| VIII Security | New settings through the authenticated settings save; no new unauthenticated path moves the roof. |
| IX Docs | User guide (close on unsafe, roof motion, alerts, safety inputs, configuration), REST, MQTT, Home Assistant; diagrams re-confirmed. |
| X Quality | Quality gate. |

Constitution 8.0.0 (MAJOR) records the relaxations of I, II and III and their risks.

## Project Structure

- lib/CloseSequenceLogic: `WeatherHold`, sequence phases for the unknown-position and blind closes
  and the N.I.N.A.-lost rule, events mapped to `needs_person`, alert wording, the needs-person
  watcher and the alert queue.
- lib/RoofLogic: automatic close from an unknown position; blind close with its stall check and report.
- lib/RoofIoLogic: the N.I.N.A.-lost override bit in the gates and the interlock; the fault record's
  blind-close flag; status fields.
- lib/SafetyInputLogic: SQMeter `reasons`, flag names as words.
- lib/AlertLogic, lib/DeviceCore, lib/ConfigModel, lib/SettingsDeps: the `needs_person` event and the
  new settings (`autoClose.unsafeHoldSeconds`, `autoClose.ninaLostClose`, `autoClose.ninaLostMinutes`,
  `roof.blindClose`, `roof.blindSagVolts`).
- lib/HaDiscovery: "Needs a person" binary sensor.
- src: CloseOnUnsafe (hold, new actions), RoofControl (blind and unknown-position close),
  RoofFaultStore, AlertDispatcher/main (queue, NVS keep), status writers.
- web: Banner, roof card Notes, settings fields, schemas, i18n.
- scripts/lto.py: link-time optimisation of the project's own objects.

## Pull requests

One PR (spec 030).

## Complexity Tracking

None outside the constitution amendment.
