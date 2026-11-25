# Implementation Plan: Heap-Safe Alert Path

**Branch**: `spec/035-alert-heap-safe` | **Spec**: [spec.md](spec.md)

## Summary

`lib/SettingsDeps` gains `alertChannels`, which runs the same `addAlertChannels` rules into a fixed
array of four entries, and `channelReason`; `channelBlocks()` uses them with the network facts only
(the Alpaca device list, a copy, isn't needed). `evaluate` reserves its exact 40 entries. The alert
queue reserves its capacity and sends one alert per step (critical first). The outbox checks the
heap with `HeapSafe::alertFits` before a step. The dispatcher reserves its records, and its HTTPS
job carries `JobChannels` (nine fields) instead of `AlertsConfig`, with `new (std::nothrow)`; the
TLS clients of the alert and poll tasks, the diagnostics summary list and the SQMeter sync copies
are allocated checked. The heap audit gains a running-path rule.

## Constitution check

- I-III (safety): no motion, interlock or decision change; the close sequence is untouched. Alerts
  are delayed by at most a few seconds in a burst and wait while memory is short; none is dropped.
- IV (Alpaca): unchanged.
- VI (size): flash 90.1% (1,653,663 of 1,835,008 bytes), under the 90% warn line's tolerance; no
  UI change.
- VIII (tests): native tests first (`test_alert_heap`), `test_alert_queue` updated for pacing.
- IX (docs): `docs/user-guide/alerts.md`, `docs/development/heritage.md`, the changelog.

## Files

- `lib/SettingsDeps` (`alertChannels`, `channelReason`, exact reserve), `lib/CloseSequenceLogic`
  (`AlertQueue` reserve and pacing), `lib/HeapSafe` (`alertFits`).
- `src/WebServerStatus.cpp`, `src/AlertOutbox.cpp`, `src/AlertDispatcher.cpp`,
  `src/SafetyPoller.cpp`, `src/WebServerDiag.cpp`, `src/SiteSync.cpp`, headers.
- `test/test_alert_heap` (new), `test/test_alert_queue`, `tools/firmware/test_heap_audit.py`.
