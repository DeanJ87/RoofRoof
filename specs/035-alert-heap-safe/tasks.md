# Tasks: Heap-Safe Alert Path

- [x] T001 Decode the crash record against its build's `.elf`; find the failing allocation (the 33rd entry's 1,280-byte growth)
- [x] T002 Native tests first: `test/test_alert_heap` (channel check allocation-free and equal to the full list, one-allocation list, push never grows, a burst one per step without allocating, the memory rule)
- [x] T003 `Deps::alertChannels` / `channelReason`; `evaluate` reserves its exact count (`lib/SettingsDeps`)
- [x] T004 `channelBlocks()` from `alertChannels` and `networkFacts()` (`src/WebServerStatus.cpp`)
- [x] T005 `AlertQueue`: reserve once, one send per step, critical first; `test_alert_queue` updated
- [x] T006 `HeapSafe::alertFits`; the outbox waits while memory is short (`src/AlertOutbox.cpp`)
- [x] T007 Dispatcher: records reserved, `JobChannels`, nothrow job and TLS client (`src/AlertDispatcher.cpp`)
- [x] T008 Audit of the other running paths: weather poll body cap and TLS client, diagnostics summaries, SQMeter sync copies
- [x] T009 Running-path rule in `tools/firmware/test_heap_audit.py`
- [x] T010 Docs (alerts, heritage), changelog; build, native tests, quality gate, flash budget, docs build
