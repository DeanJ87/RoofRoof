# Plan: An Accepted Open Reports Opening

**Spec**: [spec.md](spec.md) | **Constitution**: 12.3.0 (III, IV, V, VI, IX)

## Approach

1. **The rule** (`lib/RoofIoLogic/CommandCheck`): `CloseArmed` in `CommandContext`; a remote Open
   refused before the park gate while armed and the weather safe; `closeArmedText`.
2. **The hold** (`lib/CloseSequenceLogic/WeatherHold`): `clearsInMs`, `lengthMs`.
3. **The hand-over**: `CloseOnUnsafe::holdWeather` stores it in atomics, `handle` passes it to
   `RoofControl::setCloseArmed`; `RoofControl::checkNow` reads it. Demo core: `EmulatedRoof::checkNow`
   from its own hold.
4. **Tests** through the Alpaca router (`test_open_reports_opening`).
5. **Docs, changelog, roadmap, demo core, constitution 12.3.0.**

Why refuse rather than report Opening and then Closed: N.I.N.A. reads once after a second; an
accepted open undone in 0.3 s is the failure itself. ASCOM wants a move that can't be done refused at
the call (IV: "never silently does nothing").

Why only automations: a person at the web UI sees what happens, and constitution "Who May Set a Check
Aside" leaves a person's way past a check to the owner (F39b).

## Budgets

Six 4-byte atomics; no task, buffer or allocation (the refusal text is built only on a refusal).

## Complexity Tracking

None: nothing is relaxed.
