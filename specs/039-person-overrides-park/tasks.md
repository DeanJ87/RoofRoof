# Tasks: A Person May Open Past the Park

- [x] T001 Constitution 10.0.0: Who May Set a Check Aside; Principle II's confirmed open; III's list; Sync Impact Report
- [x] T002 `allowedOverrides`, `overridableNow`, `interlockLost(..., openParkOverridden)` (lib/RoofIoLogic)
- [x] T003 `MoveGuard::ran` filtered by origin, `openPark`, `inEffect()`, `parseOverrides` text, `parkOverrideText`
- [x] T004 Firmware: callers, the interlock, the snapshot's overrides, the log line, `takeOverrideAlert`, `CloseOnUnsafe::parkOverridden`, main loop
- [x] T005 `AlertType::CheckOverridden` (`check_overridden`), its tag, `CloseSeq::parkOverrideAlert`
- [x] T006 Native tests: `test_person_overrides` (audit rows, guard, interlock, end to end, alert, body); existing tests on the new `ran` signature; alert settings test exempts the new type
- [x] T007 F31: `test_weather_refusal_reasons` (HTTP and MQTT, Client and MQTT, Alpaca error, MQTT result, refused stop)
- [x] T008 Web: `movePlan`, `MoveDialog`, `RoofButtons`, i18n; Vitest (movePlan, RoofMove)
- [x] T009 Demo core follows the guard API; rebuilt (`tools/demo-core/build.sh`)
- [x] T010 Docs: close-on-unsafe, dashboard, alerts, REST; operations.json and OpenAPI; changelog; roadmap
- [x] T011 The override audit table and follow-ups in spec.md
- [ ] T012 Bench (bench mode, no motor): Open anyway with N.I.N.A. not parked opens and isn't stopped by the park; Stop and an unsafe weather still stop it; the alert arrives (owner, before the roof)
