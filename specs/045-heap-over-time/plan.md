# Implementation Plan: The Heap Over a Session

**Branch**: `spec/045-heap-over-time` | **Spec**: [spec.md](spec.md)

## Summary

Take the web page's per-frame and per-load heap off the device (frames in place, cached files,
banners from the live status), keep the recent alerts in static room, size the save's strings once
and check its real need, and record the heap's trend and what cuts it.

## Constitution check

- **I/II/III (safety)**: no change to motion, close, park, weather; alerts only in how the recent
  list is kept.
- **V**: `ChunkText::over`, `FixedRing`, `copyCut`, `HeapTrend` and the census's free map are in
  `lib/HeapSafe` with native tests (test/test_heap_over_time); the banners' hook with Vitest.
- **VI (budgets)**: static data +9.3 KB, net kept +4.7 KB (research R5), recorded in
  `heap-budget.json`; stack depth unchanged; flash 90.9%.
- **VII**: no UI change visible (the banners read the same state).
- **IX (docs)**: REST (status fields, census sections), troubleshooting, heap budget page,
  changelog, schema, OpenAPI.

## Design

| Piece | Where |
|---|---|
| frames in place | `ChunkText::over`, `WebReply::pooledFrame` / `frameAll` / `frameTo` / `noteSkipped`, `WebServer::broadcastStatusData`, `sendFirstStatus`; stack snapshot in `closeStalledClients` |
| cached files | `WebServer::setupStaticRoutes` (`ASSET_CACHE_CONTROL` on `/assets/`) |
| banners | `web/src/hooks/useLiveStatus.ts`, `NeedsPersonBanner`, `TestBoxBanner` |
| recent alerts | `HeapSafe::FixedRing`, `copyCut`; `AlertRecord` fixed; `AlertDispatcher::visitRecent`; `handleAlertsRecent` |
| save | `WebServerApi.cpp` `saveStartBytes` / `saveBlockFits`, body `reserve`; `Config::toJson` `reserve` |
| trend | `HeapSafe::HeapTrend`, `keepHeapReserve()` in `loop()`, `heap8Bit.largestLow15m/60m`, census `Trend` |
| free map | `HeapCensus::FreeSpan`, `writeFreeMap` |
| soak | `ws_soak.py` `--page-load`, `--cached`, `--max-drift`, `drift()`, `page_assets()` |
