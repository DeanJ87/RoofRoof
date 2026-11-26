# Tasks: A Move Finishes When N.I.N.A. Stops Answering

- [x] T001 Constitution 12.0.0: Principle II's re-check, "a command is a command", Sync Impact Report
- [x] T002 `CloseHold::unanswered` (ParkHold.cpp)
- [x] T003 Gates: `parkUnanswered`, `closeMayContinue`, `parkHoldsFresh`, `movesOnStale`, `interlockRecordBits`; `currentGates`
- [x] T004 `StaleRide` and its texts; `RoofControl::stepStaleRide`, `takeStaleAlert`; main loop and `CloseOnUnsafe::finishedOnStale`
- [x] T005 `AlertType::FinishedUnconfirmed` (`finished_without_park`), ntfy tag, `CloseSeq::staleFinishAlert`
- [x] T006 Fault text by direction (`InterlockDuringOpen`), firmware and demo
- [x] T007 `PollSchedule`, `HttpLimits`; SafetyPoller uses them; `setRoofMoving` from the main loop
- [x] T008 Native tests: test_moves_finish_on_stale, test_poll_schedule; updated test_park_grace, test_roof_status, test_roof_alert_settings
- [x] T009 Demo core rebuilt
- [x] T010 Docs: close-on-unsafe, safety-inputs, alerts, REST; changelog
- [ ] T011 Bench and roof checks (owner; spec.md "Owner checks")
