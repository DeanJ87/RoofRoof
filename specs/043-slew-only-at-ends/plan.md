# Plan: The Telescope Moves Only With the Roof at an End

**Spec**: [spec.md](spec.md) | **Constitution**: 10.2.0 (I-IV, V, VII, IX)

## Approach

1. **The predicate** (`lib/RoofIoLogic/MountMove`): `roofAtEnd`, `mountMayMove`,
   `parkRequestAllowed` (from a `Roof::Status` or a `Roof::State`), the texts (`roofPlaceText`,
   `mountBlockedText`) and the Switch's fixed table (`MountSwitch`, names, descriptions, values).
2. **The automatic close** (`lib/CloseSequenceLogic`): `requestIfDue(in)` returns no request unless
   the roof is at an end; `betweenEndsUnparked` (not moving, not at an end, not parked, no
   N.I.N.A.-lost close) enters `CannotMove` with `Cause::RoofBetweenEnds` from idle and from a wait;
   `cannotMove()` (new `CloseSequenceEnds.cpp`) ends it: idle once safe, an attempt at once at an
   end or with the mount parked (or the back-off a failed close left). The alert is the existing
   `CannotClose` (`needs_person`, reminded), its reason `Reasons::betweenEnds`.
3. **The park wait** (`MoveGuard`): `Request::roof` (the default, unknown, never waits); `admit`
   refuses for the park with `parkWaitUnsafe` when not at an end; `step` ends a wait whose roof
   left its end.
4. **The senders**: `CloseOnUnsafe::carryOut` and the main loop check
   `RoofControl::parkRequestAllowed()` (the roof's state now) before `SafetyInputs::requestPark`;
   the demo the same.
5. **Alpaca**: `Backend::switches()` (default none), `switchDeviceInfo`, `AlpacaSwitch.cpp`
   (ISwitchV3, read-only), `RoofIo::switchReadings`; the firmware, the simulator and the demo list
   the Switch after the Dome. ConformU in CI runs the Switch with
   `tools/alpaca-sim/conformu-switch.json` (write tests on).
6. **Status and UI**: `roof.mountMayMove`; the Roof card's Telescope row; schema, OpenAPI example,
   inventory, i18n.
7. **Docs**: alpaca.md (the Switch, hardening), close-on-unsafe.md (the new section, DIA-22),
   alerts.md, dashboard.md, rest.md, changelog, roadmap; constitution 10.2.0.

## Constitution check

- I-III: only tightened (fewer park requests, no new motion). No path relaxes a check; the
  person's confirmed open and close are unchanged and still the only way past the park.
- IV: the Switch passes ConformU; the Dome's members, numbers and UniqueID are unchanged (index 0).
- V: every decision in `lib/`, native tests for each transition and boundary.
- VI: flash +8 KB (91.4 %, under the 95 % limit; `strtod` avoided); no new task, no blocking.
- VII: one row from the shared parts, DS-02/04/08/20.
- IX: docs and the safety diagrams (DIA-18, DIA-22) move with it.

## Complexity tracking

None.
