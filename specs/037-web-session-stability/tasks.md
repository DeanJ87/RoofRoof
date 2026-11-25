# Tasks: Stable Live Pages Under Load

- [x] T001 Native test: nothrow new never calls the throwing form, returns null (test/test_nothrow_new)
- [x] T002 `lib/HeapSafe/src/NothrowNew.cpp`; link check with `nm`
- [x] T003 `HeapSafe::acceptFits` + test; guard in `Ipv6Network::listen`
- [x] T004 `ConnectionBudget::TOO_MANY_PAGES`, `HEARTBEAT` + test
- [x] T005 Refuse past the limit (`prepareWebSocketClient`, `capWebSocketClients`), status `refused`
- [x] T006 `HeapSafe::FramePool` + test (test/test_frame_pool); `WebReply` uses it; `WS_MAX_QUEUED_MESSAGES=1`
- [x] T007 Heartbeat in place of a skipped status frame
- [x] T008 Page: `liveStream.ts`, `useWebSocket.ts` (heartbeat, lastKnown, tooMany, back-off), tests
- [x] T009 Dashboard and System keep the last values greyed (`LiveStale`); strings
- [x] T010 `tools/soak/ws_soak.py` + `test_ws_soak.py` in CI
- [x] T011 Contracts, OpenAPI, REST and troubleshooting docs (`refused`, 4429, heartbeat)
- [x] T013 `HeapSafe::replySlotFree` + test; `heap8Bit` in the status, contracts, docs; soak reads it
- [x] T012 Bench: soak build (moving cadence) 10 min under the R2 load; release build flashed and soaked
