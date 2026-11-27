# Tasks: Alerts Off the Control Loop

- [x] T001 Constitution 12.4.0: Principle VI's "alerts never touch roof control", Sync Impact Report
- [x] T002 `HeapSafe::tlsFits` and the TLS floor (HeapGuard.h/.cpp)
- [x] T003 `Alerts::SendQueue`, `SendGate`, `sendWaitText` (AlertSend.h/.cpp)
- [x] T004 Dispatcher hand-over: the fixed queue, spinlock, counting semaphore, `critical`, "Queue full", `deliveryStats`
- [x] T005 Alert task (`src/AlertSender.cpp`): core 0, priority 1, `waitToSend`, `mayStart` under `TlsLock`, `sendMeasured`, stack least-left
- [x] T006 `WebServer`: `roofMoving` to the dispatcher; the counts in `GET /api/diag/heap`
- [x] T007 `stack_depth.py`: the alert task; device-message catalogue: the new texts
- [x] T008 Native tests: test_alert_send (queue, floor, gate, a move with alerts)
- [x] T009 Docs: alerts, heap budget, REST; changelog; roadmap
- [ ] T010 Bench check (owner; spec.md "Owner checks", plan.md "Bench note")
