# Tasks: Heap Headroom

- [x] T001 Native test and `HeapSafe::HeapCensus` (test/test_heap_census)
- [x] T002 `src/HeapMap.cpp`, `GET /api/diag/heap` (auth, 409 while moving), routes, operations, OpenAPI, REST docs
- [x] T003 Native test and `HeapSafe::HeapReserve` (test/test_heap_reserve); armed in `setup()`, retaken in `loop()`, guard, `heap8Bit.reserveSpent` (schema, types, inventory, example, troubleshooting)
- [x] T004 Stacks: loop 10 KB, `async_tcp` 8 KB, `esp_timer` 4 KB; send buffer halved, tried on the bench and reverted (research R5)
- [x] T005 `tools/firmware/heap_budget.py` + tests + `heap-budget.json`; CI step
- [x] T006 `ws_soak.py` `--free-floor`, `--clients 0`, `verdict()` + test
- [x] T007 Developer page `docs/development/heap-budget.md` with the budget table; roadmap; changelog
- [x] T009 `ConnectionBudget::NetworkSettle` + test; `NetState::tick/settledFor`; MQTT reconnect and the alert outbox wait
- [ ] T008 Bench: flash (closed, at rest), census at rest, at-rest floor check, 10-minute soak; numbers into the budget table and research R6
