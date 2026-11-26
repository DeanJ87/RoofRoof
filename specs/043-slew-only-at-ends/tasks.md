# Tasks: The Telescope Moves Only With the Roof at an End

- [x] T001 Constitution 10.2.0: Principle II's rule, Principle IV's Switch, Sync Impact Report
- [x] T002 `MountMove` (predicate, texts, switch table) in lib/RoofIoLogic
- [x] T003 Close sequence: no request between the ends, `Cause::RoofBetweenEnds`, `cannotMove()`, texts and alert cause
- [x] T004 MoveGuard: `Request::roof`, no wait between the ends, a wait ends when the roof leaves its end
- [x] T005 Senders: `CloseOnUnsafe::carryOut`, main loop (`RoofControl::parkRequestAllowed`), demo
- [x] T006 Needs-person banner line names where the roof is
- [x] T007 Alpaca Switch: router, members, `switchReadings`, firmware backend, simulator, demo
- [x] T008 ConformU: CI runs the Switch (conformance with writes on, protocol); local run (4.3.0) clean
- [x] T009 Native tests: test_mount_may_move, test_alpaca_switch, test_move_guard, test_needs_person; updated close tests
- [x] T010 Status `roof.mountMayMove`: schema, OpenAPI example and document, types, mocks
- [x] T011 Roof card Telescope row, i18n, inventory, Playwright check
- [x] T012 Demo core rebuilt
- [x] T013 Docs: alpaca (Switch, hardening), close-on-unsafe (+DIA-22), alerts, dashboard, REST, changelog, roadmap
- [ ] T014 Bench (bench mode, no motor) and N.I.N.A. checks (owner; spec.md "Owner checks")
