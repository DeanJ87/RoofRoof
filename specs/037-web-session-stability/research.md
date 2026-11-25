# Research: Stable Live Pages Under Load

## R1 Crash records 24 and 25

Both on `async_tcp`, both `abort()` <- `operator new(size_t)` <- `operator new(size_t, nothrow_t)`:
in `AsyncWebServerRequest::create` (a new connection) and in
`AsyncAbstractResponse::write_send_buffs` (a response's send buffer). The framework's sdkconfig has
`CONFIG_COMPILER_CXX_EXCEPTIONS` unset, so libstdc++'s nothrow form (throwing new inside a
try/catch) aborts. Both call sites handle `nullptr`. Decision: replace the nothrow forms project
wide with malloc-based ones (same allocator as the throwing form, so `operator delete` matches).
A native test replaces the throwing form with a counter and shows the stock nothrow form calls it
(fails without the replacement) and ours doesn't.

## R2 Live-update limit churn

Bench, firmware before this spec, roof closed, the owner's pages plus soak pages (4 on
`/ws/status`), `/api/status` at 2 Hz, `/api/diag` every 10 s, Alpaca at 1 Hz: `replaced` rose 23
in 4 minutes, 4 of 24 status samples timed out (8 s), `refused` +24 and `skippedFrames` +4 for
memory. The log before record 24 shows the same pattern: a connect and a disconnect of the client
three ids older every 1-2 s. Decision: refuse the newcomer (close 4429 from the loop task).

## R2b The heap figures

`ESP.getMaxAllocHeap()` and `ESP.getFreeHeap()` use `MALLOC_CAP_INTERNAL`, which includes the
IRAM heap (32-bit access only, about 59 KB on this build); `maxAllocHeap` read 59,380 B in every
sample whatever the load. `heap_caps_*(MALLOC_CAP_8BIT)` (what the memory guard and the crash
snapshot use) showed about 45 KB free and 19-43 KB largest under load, and a lowest free of
2.1 KB since start with `/api/status` at 2 Hz. Decision: report `heap8Bit`; cap built API replies
in flight at 2.

## R3 Frames

`WebReply::frameOf` allocated a 4.4 KB vector per broadcast (500 ms while moving, plus each change
and each new client); `WS_MAX_QUEUED_MESSAGES` 4 let a slow client hold 4. Decision: 2 pooled 5 KB
buffers reserved at start, one queued message per client; a frame with no free buffer is skipped
and a heartbeat goes instead.

## R4 Page

`LIVE_QUIET_MS` 5 s against a 2 s rest cadence; the reconnect reset `retryCount` on open, so a
socket opened and closed again by the device retried every ~1 s; a drop cleared the page.
Decision: 10 s, back-off reset on a reading, last values greyed, 4429 shown and retried every 60 s.

## R5 Measurements after

See the PR: `tools/soak/ws_soak.py` with the soak build (moving cadence at rest) and then the
release build.
