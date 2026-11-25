# Implementation Plan: Heap Headroom

**Branch**: `spec/038-heap-headroom` | **Spec**: [spec.md](spec.md)

## Summary

Measure the heap on the board (a census endpoint), give back what task stacks never use, halve the
send buffer a connection may hold, turn the throwing `operator new`'s abort into a recoverable event
with a small reserve, and keep the savings with a CI budget and a bench floor.

## Constitution check

- **I/II/III (safety)**: no change to motion, close, park, weather or alerts. The roof task, its
  watchdog timer (now on a 4 KB timer stack; the callback is a GPIO write and a comparison) and the
  safety poller keep their behaviour; the census is refused while the roof moves.
- **VI (budgets)**: flash +1.4 KB (90.7%); LittleFS and UI unchanged; the heap budget is a new
  CI check.
- **STRUCT-01**: `HeapCensus` and `HeapReserve` are in `lib/HeapSafe` with native tests; `src/`
  only walks the heap and wires them in.
- **VIII**: the census needs the password when protection is on.
- **IX (docs)**: REST, troubleshooting, a developer page with the budget table.

## Design

| Piece | Where |
|---|---|
| census | `lib/HeapSafe` `HeapCensus` (fixed arrays, `add` per block), `src/HeapMap.cpp` (`heap_caps_walk`, `uxTaskGetSystemState`), `WebServer::handleHeapCensus` |
| reserve | `lib/HeapSafe` `HeapReserve`, `armReserve()` first in `setup()`, `keepHeapReserve()` in `loop()`, `WebReply::installGuard` (`guardBlock`) |
| stacks | `SET_LOOP_TASK_STACK_SIZE(10 * 1024)`, `-DCONFIG_ASYNC_TCP_STACK_SIZE=8192`, `CONFIG_ESP_TIMER_TASK_STACK_SIZE=4096` |
| send buffer | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=2872` |
| status | `heap8Bit.reserveSpent`; schema, types, inventory, example |
| CI budget | `tools/firmware/heap_budget.py`, `heap-budget.json`, tests; build workflow |
| bench floor | `tools/soak/ws_soak.py` `--free-floor`, `--clients 0`, `verdict()` |
