# Research: Heap Headroom

## R1 Where the heap goes (main 4a26f82, before this spec)

From the build (`.elf` sections, `sdkconfig.esp32dev`), the start-up trace (`/api/status`
`heapStages`: free is all internal RAM, largest is the 8-bit heap) and the crash snapshots:

| Stage | Internal free | 8-bit largest | Taken by the stage |
|---|---:|---:|---|
| config loaded | 279,652 | 110,580 | core, loop task, timer task, settings |
| roof | 276,100 | 110,580 | roof task (6 KB) |
| wifi | 202,188 | 110,580 | WiFi driver, lwIP, mDNS, their tasks (74 KB, outside the largest region) |
| mqtt / safety inputs | 186,676 | 110,580 | MQTT buffer, `safetyPoll` task (8 KB) |
| web server routes | 157,376 | 94,196 | handlers, `async_tcp` task (11 KB), frame pool (10 KB) |
| setup complete | 134,744 | 69,620 | alert outbox, Alpaca discovery, listener |

Static data is 61.2 KB (`.dram0.data` 21.1 KB + `.dram0.bss` 40.1 KB); the largest symbols are the
roof's state (4.7 KB), the timer interrupt stack (4.2 KB), WiFi's connection manager (3.9 KB) and
the close sequence (2.5 KB): nothing cheap to cut.

A fresh boot of main reads `heap8Bit` free 64.8 KB, largest 38.9 KB; after a while at rest 21-37 KB;
under spec 037's soak 8-10 KB.

## R2 Task stacks: size against measured peak

The least stack left since start, from crash records 24-26 and `/api/status` `stackFree` over the
bench and shakedown runs (hours, settings saves, uploads, alerts):

| Task | Stack | Least left | Peak | Decision |
|---|---:|---:|---:|---|
| loop | 12,288 | 4,996 | 7,292 | 10,240 (2.9 KB spare) |
| async_tcp | 11,264 | 6,264 | 5,000 | 8,192 (3.2 KB spare) |
| esp_timer | 8,192 | not reported | short callbacks | 4,096 (IDF default 3,584) |
| roof_motion | 6,144 | 2,836 | 3,308 | unchanged |
| safetyPoll | 8,192 | 3,616 | 4,576 (HTTP only) | unchanged: an https SQMeter needs a TLS handshake |
| alerts | 8,192 | - | TLS handshake | unchanged; started on demand and exits when idle |

The timer task runs the roof's 10 ms motor watchdog (a GPIO write), the restart timer (a check and
`ESP.restart()`) and the WiFi driver's timers. The census (`GET /api/diag/heap`) now reports every
task's least stack left, so these margins can be watched on the board.

## R3 What aborts

`operator new` (the throwing form) calls malloc in a loop; on failure it calls the installed
new-handler, and with none it throws `std::bad_alloc`, which without exceptions is `abort()`.
`std::string`, `std::list` (the web server's headers and handlers' lists), `std::function` and
`std::shared_ptr` all use it. Crash record 26 (37's build, `async_tcp`, largest block 10.7 KB,
`heap8Bit.minFree` 476 B since that boot) is consistent with a burst exhausting the heap during
request handling. Arduino `String` uses realloc and fails softly (no abort).

Decision: a new-handler that frees a 2 KB reserve so the allocation succeeds; the guard treats a
spent reserve as a short heap (non-essential requests "busy") until the loop retakes it. The
handler doesn't allocate or log. A second failure before the retake aborts as before.

## R4 Options considered and not taken

- **IPv6 out (`CONFIG_LWIP_IPV6=n`)**: IPv6 is a setting (spec 023, off by default). Off, it adds
  no per-connection allocation: the one listener (`Ipv6Network::listen`) is the same `AsyncServer`
  the web server would make, and checks the peer only for IPv6 clients. Compiling it out would
  save about 1 KB of neighbour caches and drop a feature.
- **WiFi buffers**: the Arduino core sets them at run time (`WiFiGeneric.cpp`: 4 static RX,
  32 dynamic RX, 32 dynamic TX, 4 cached TX) whatever `sdkconfig` says; the dynamic ones are taken
  only while traffic is in flight. Changing them (or AMPDU) needs `useStaticBuffers(true)` and its
  own reconnect and throughput soak: a follow-up.
- **Bluetooth**: `CONFIG_BT_ENABLED=n`, so no controller memory is reserved (the census's region
  list shows no gap for it).
- **TLS**: already `CONFIG_MBEDTLS_DYNAMIC_BUFFER` with asymmetric 16 KB in / 4 KB out (a server
  without max-fragment negotiation can send 16 KB records). Freeing the CA and config after the
  handshake changes the alert path's TLS objects' life; not without its own alert soak.
- **The 3 KB status pool and the 2 x 9.7 KB settings blocks**: transient, behind the memory guard
  and the replies-in-flight cap; not at rest.
- **Heap poisoning off**: about 10 B per block (some 900 blocks) but loses overrun detection.

## R5 Send buffers

`CONFIG_LWIP_TCP_SND_BUF_DEFAULT` 5,744 B let each connection hold 4 segments of unacknowledged
data: with 4 pages each sent a 4.4 KB status frame and two status replies in flight, up to about
30 KB at once. 2 segments (2,872 B, lwIP's minimum of 2 x MSS) halves that peak.

## R5b The reconnect burst

On main at rest, the end of a 2-minute access-point outage took `heap8Bit` to 24.0 KB free, 11.3 KB
largest and 352 B lowest since start (one `/api/status` answered busy): WiFi, MQTT's connect and
Home Assistant discovery, the safety polls, N.I.N.A.'s park reads, NTP and the outbox's queued
outage alerts all ran in the same seconds. Decision: the safety polls first (they decide the
roof), MQTT 3 s later, queued alerts 8 s later (`ConnectionBudget::NetworkSettle`).

## R6 Measurements after

See the budget table in `docs/development/heap-budget.md` (filled from the bench).
