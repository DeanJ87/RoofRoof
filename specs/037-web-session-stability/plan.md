# Implementation Plan: Stable Live Pages Under Load

**Branch**: `spec/037-web-session-stability` | **Spec**: [spec.md](spec.md)

## Summary

Make running out of memory survivable (nothrow new returns null, accept refused when nearly
empty), stop the live-update limit from churning (refuse the newcomer with 4429), stop live frames
fragmenting the heap (pooled buffers, one queued message per client), and make the page tolerant
(10 s, heartbeat, last values greyed, back-off that can't loop, "Too many pages").

## Constitution check

- **I/II (safety)**: no change to motion, close or park logic. The page never treats an old
  reading as live: Open and Close stay off without a reading in the last 10 s; Stop always shows.
- **VI (budgets)**: flash +2 KB (90.3%); UI +~0.6 KB gzipped; 10 KB heap reserved for frames.
- **STRUCT-01**: `FramePool`, `NothrowNew`, `acceptFits`, the 4429 code and heartbeat are in
  `lib/` with native tests; `src/` only wires them in.
- **IX (docs)**: REST and troubleshooting updated (`refused`, 4429, heartbeat).

## Design

| Piece | Where |
|---|---|
| nothrow `new`/`new[]`/`delete` via malloc | `lib/HeapSafe/src/NothrowNew.cpp`, referenced from `Ipv6Network::listen` so it is always linked (checked with `nm`: one definition) |
| accept guard | `HeapSafe::acceptFits`, `Ipv6Network::listen` |
| refuse past the limit | `WebServer::prepareWebSocketClient` marks it (connect callback), `capWebSocketClients` closes it from the loop task (closing inside the library's callback could free the client it still holds); `cleanupClients(UINT16_MAX)` only frees |
| replies in flight | `HeapSafe::replySlotFree`, counted by `WebReply`'s `ChunkTextResponse` |
| `heap8Bit` | `WebServer::appendMemory` |
| pooled frames | `HeapSafe::FramePool` (2 x 5 KB, `use_count()==1` means free), `WebReply::frameOf`; `-DWS_MAX_QUEUED_MESSAGES=1` |
| heartbeat | `WebServer::broadcastStatusData`, `sendFirstStatus` |
| page | `lib/liveStream.ts` (10 s, 4429, 60 s, `isHeartbeat`), `hooks/useWebSocket.ts` (`lastKnown`, `tooMany`, back-off reset on a reading), `LiveWaiting.tsx` (`LiveStale`), Dashboard, System |
| soak | `tools/soak/ws_soak.py` (+ unit tests in CI); a soak build with `-DROOFROOF_SOAK_MOVING_CADENCE` (via `PLATFORMIO_BUILD_FLAGS`, never released) sends at the moving rate with the roof at rest |
