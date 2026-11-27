# Plan: N.I.N.A.'s Frozen Mount Values Are Not a Reading

**Spec**: [spec.md](spec.md) | **Constitution**: 12.2.0 (II, V, VI, IX)

## Approach

1. **The field** (`lib/NinaLogic`): `MountReading` gains `hasSiderealTime`, `siderealHours`,
   `frozen`; `ApiV2` filters and reads `SiderealTime`.
2. **The rule** (`lib/NinaLogic/MountClock.h`, `MountClock.cpp`): `MountClockWatch::apply` marks a
   frozen reading unreadable; `reset` for new settings; `siderealStepMs` for the wrap.
3. **The poller**: `pollNina` hands its reading to `watchClock` (new `src/SafetyPollerClock.cpp`,
   keeping `SafetyPoller.cpp` under the 600-line limit: 597) before storing it; the watch and its
   generation are poller-task members.
4. **The text**: `ParkResult::ninaFrozen`, set in `evaluatePark`; `describe` words it.
5. **Tests, docs, changelog, roadmap, demo core, constitution 12.2.0.**

Why an unreadable reading and not a new `ReadingStatus`: every rule that must treat it as
"N.I.N.A. not read" (the park rule, spec 021's ride-through, spec 047's unanswered hold, spec 050's
sender, spec 030's N.I.N.A.-lost watch, the alerts, the test box's reasons) already does for
`Unreadable`; a new status would have to be added to each of them, to the REST and MQTT state names
and to the web UI's types, and missing one would be a safety hole. The text tells it apart.

## Budgets

No task, no buffer kept: the watch is 40 bytes in the poller object. The filter document grows by
64 bytes on the poller's stack for the parse; the parsed document is unchanged.

## Complexity Tracking

None: Principle II gains a rule (MINOR); nothing is relaxed.
