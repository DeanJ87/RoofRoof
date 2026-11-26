# Implementation Plan: A Person May Open Past the Park

**Branch**: `spec/039-person-overrides-park` | **Spec**: [spec.md](spec.md)

## Summary

Let a person's Open carry the park override spec 027 gave a Close, keep it for that one open so
the park interlock doesn't stop it, log and alert it, filter every override by origin where it is
kept, and pin finding F31 with an end-to-end test.

## Constitution check

- **New section, Who May Set a Check Aside, and II (MAJOR, 10.0.0)**: a person's confirmed open
  relaxes Principle II, so the amendment states the risk and the mitigations (password, red
  warning, one open, never an automation, log and alert).
- **I/III**: unchanged; the weather and daylight stops, every fault and Stop still stop the open.
- **V**: `test_person_overrides` (the audit rows, the gate, the guard, the interlock, an end-to-end
  open on the simulated roof, the alert), `test_weather_refusal_reasons` (F31); Vitest for the
  dialog.
- **VI (heap)**: no allocation on the control step. The override text is built once per person's
  command (not the hot path) and copied once for the main loop; the main loop's string is empty
  (no heap) unless an override happened. One `std::string` and one atomic added to `RoofControl`.
- **VII**: the move dialog keeps DS-11 (the modal with the Stop bar); the warning is the existing
  `Note` in its "bad" tone; no new styles.
- **VIII**: the override is still only parsed from the authenticated REST body; Alpaca and MQTT
  pass none, and the bits are filtered by origin.
- **IX**: close-on-unsafe, dashboard, alerts, REST and OpenAPI updated.

## Design

| Piece | Where |
|---|---|
| who may | `RoofIo::allowedOverrides`: a person's Open gets `OverridePark` |
| offered | `RoofIo::overridableNow(Open)`: `OverridePark` while `!parkHolds` |
| kept | `MoveGuard::ran(command, origin, overrides, gates)` filters by origin; `openPark` for one open, cleared once the roof isn't opening; `inEffect()` for the status |
| interlock | `RoofIo::interlockLost(..., openParkOverridden)`: not for an Opening with it, never for a leg or a jog |
| log and alert | `RoofIo::parkOverrideText`; `RoofControl::command` notes it, logs outside the lock; `takeOverrideAlert` on the main loop; `CloseSeq::parkOverrideAlert` (`AlertType::CheckOverridden`, Normal) through the alert outbox |
| UI | `movePlan` (`parkOnly`, the park named), `MoveDialog` (red note, Wait only when park-only), `RoofButtons` (a park-only Open goes at once, as a Close) |
| demo | `tools/demo-core` follows the guard API; core rebuilt |
| F31 | `test_weather_refusal_reasons`: SQMeter's document over HTTP and MQTT to the Alpaca error, the MQTT result and a refused stop |

## F31 finding

The library path already carries SQMeter's reasons into the refusal (spec 030): the end-to-end test
passes on main's code too, for HTTP and MQTT, Client and MQTT origins. A bare "Weather not safe:
unsafe" can't come from the current verdict text (it always names a source and why). Likely origins
of the report: firmware older than spec 030, or SQMeter's MQTT `<base>/safety` not republished when
its reasons change while it stays unsafe (SQMeter publishes on a change of `safe` or every
republish period), which leaves RoofRoof's MQTT source with the earlier reasons. The test now pins
the behaviour; the SQMeter side is noted for SQMeter.
