# Research: The Heap Over a Session

## R1 What the board showed (main e3a09a4)

- **F40** (`shakedown/runs/heap_before_restart_0835.json`, uptime 4,433 s): `heap8Bit` free
  41.8 KB, largest 20,468 B, lowest since start 352 B, reserve given up 8 times; `heapStages`
  "setup complete" largest 77,812 B. Every settings save refused.
- **F40b** (coordinator, 08:45): fresh start, harness + N.I.N.A. + roof moving + a headless
  Chromium on the dashboard: largest 61 KB → 9.7 KB in 10 minutes, `/api/status` stopped
  answering; killing only the page: 32.7 KB within seconds (free 69.9 KB, lowest 440 B, reserve 1).
- **Census series** (`GET /api/diag/heap` every ~2 minutes at rest between moves, same boot, page
  killed at about 08:45):

  | Time | Free | Largest | Region 0x3ffe4350 free / largest / blocks used/free | Blocks in use |
  |---|---:|---:|---|---:|
  | 08:40 (up 2.5 min) | 70,988 | 61,428 | 69,396 / 63,312 / 111/23 | 663 |
  | 08:54 | 67,336 | 36,852 | 65,832 / 38,692 / 127/20 | 701 |
  | 09:03 | 64,516 | 38,900 | 60,884 / 38,956 / 130/33 | 715 |
  | 09:12 | 66,004 | 38,900 | 64,640 / 38,956 / 129/27 | 713 |

  The free heap is unchanged; the region holding the largest block was cut once, about 38 KB in,
  during the minutes the page was open, and stayed cut with the page gone: 18 more blocks in use in
  that region and about 50 more on the heap than at start, i.e. allocations made at run time that
  live on. `/api/status` `connections.shortOfMemory.refused` rose about once every 6 s all along
  (49 → 327 in 30 minutes): the two-replies cap (spec 037) turning polls away.
- `heap_caps_get_largest_free_block` reports what malloc is sure to give (TLSF's class
  boundaries): 61,428 = 60 KB − 12, 20,468 = 20 KB − 12. A fixed rule "at least 20 KB" fails at
  a reported 20,468 even when a 20,480 B block is there.

## R2 What a page costs the device (the F40b path)

Per page with the dashboard open:

| Source | Before | After |
|---|---|---|
| Live status frame, every 2 s (500 ms while moving) | 3 KB section pool + 5 x 1 KB pieces + copy into a pooled buffer | 3 KB section pool only; written in place in the pooled buffer |
| A new client's first frame (on the network task) | same | same as the broadcast |
| Banners: test box (5 s while set, 30 s otherwise) + needs a person (10 s) | 0.3 `GET /api/status` a second: each a 3 KB pool, 5 KB pieces held until sent, 2.9 KB send buffer, a request, and one of the two reply slots | none while the live status arrives |
| Opening or reloading the page (`Cache-Control: no-store` on everything) | index.html, a 154 KB script, CSS, two fonts and icons (about 210 KB) over up to 6 connections at once: each about 1 KB request, a LittleFS file with its 512 B cache, 2.9 KB send buffer per round and up to 5.7 KB unacknowledged in lwIP, so 40-60 KB at once | index.html (431 B) once the files are cached |

The page load is the size of the deep dips (lowest free 352-440 B since start, the reserve given
up): six streams at about 10 KB each beside the regular load leave nothing. The polls were the
replies the cap turned away. The per-client send of each frame (lwIP copies the 4.4 KB frame into
its own segments for each client until acknowledged) is the web server library's and is unchanged.

## R3 What lives on after it was made

Allocations made at run time and kept for minutes are what cut the largest block: TLSF takes a
block from the front of the smallest free block of a fitting class, so a lasting allocation made
while transient ones sit in the large block lands behind them, and stays when they are freed.

| Kept at run time | Before | After |
|---|---|---|
| Recent alerts (20) | `std::vector<AlertRecord>`: per alert a title and message string (and their copies in every `recent()` read, which copied the whole list onto the heap) kept until 20 more alerts; the list's 4.6 KB reserved at start | fixed records in a static ring (`HeapSafe::FixedRing`, 8.2 KB .bss); read through a visitor under the lock |
| WebSocket bookkeeping | `std::map` copied every broadcast (a node per client) | copied onto the stack (up to 8) |
| MQTT read buffer (`DualStackClient::rx`) | grown 256 → 4,096 B as packets came, each step a new lasting block | reserved whole when MQTT starts (R3b) |
| A settings save's strings | `std::string` grown by doubling: 8 KB of settings asked for 16 KB, beside the old 8 KB | sized once from `measureJson` |
| TCP PCBs, the web server's request objects and WebSocket queues | at most 16 PCBs (TIME_WAIT included, `CONFIG_LWIP_MAX_ACTIVE_TCP`), a client's deque nodes | unchanged (library, bounded) |

## R3b Without a page (coordinator, 09:20)

With no page open since 08:46, the same boot fell to a 10.2 KB largest block (reserve given up
once) after about 50 minutes: harness `/api/status` at 1 Hz with `/api/diag/moves` and
`/api/config` now and then, N.I.N.A. on Alpaca at 1-2 Hz, MQTT with moving publishes, a weather
poll every 5 s against a fake SQMeter answering slowly (5-60 s) or timing out at 3 s (C04b), about
10 moves, alerts, and the close sequence's supply-low retry every minute. The sampler (every 30 s)
read the base at 38.9 KB between moves and dips to 17-24 KB during them: the cut from R1 stays,
and the dips are what is in flight at once on top of it (the 9.7 KB `GET /api/config` pair, a
status reply's 12 KB, the poll's client).

- **The weather poll, slow or timing out**: per poll an `HTTPClient` (its strings), the network
  client's 1,436 B receive buffer, the socket and PCB, the body (up to the reply's length) and its
  parse documents, all freed at the poll's end (`http.end()`, scope), at most 6 s
  (`HTTP_TOTAL_MS`). Nothing found that outlives a poll, but a server that answers after 5 s keeps
  the poll's memory in use almost all the time, so other tasks' blocks are placed around it; the
  closed socket's PCB waits up to 120 s in TIME_WAIT (within the 16).
- **The MQTT connection's read buffer** (`DualStackClient::rx`, up to 4 KB): grown in steps as
  packets came, each step a new block made at run time and kept for the connection's life. Now
  taken whole when MQTT starts (counted in the budget).
- **The close sequence's retry and the alerts**: each alert was two lasting strings in the recent
  list (R3, fixed); the queued alert (`CloseSeq::AlertQueue`, room taken once) holds its copy only
  until a channel takes it.

The census's new free map is what settles which of these sits between the free blocks on the
board.

## R4 The settings save under a cut heap

The save's largest single allocations: `Config::applyJson`'s document (body + 6.5 KB, at least
8.5 KB), `Config::toJson`'s document (9.5 KB) and its string, and the save task's stack (10 KB,
fix 041). The fixed rule (largest at least 20 KB) refused every save once the heap reported
20,468 B. Now: before the task starts, max(document, stack) + 2 KB must fit; once it runs (its
stack taken), the document must still fit, else 503.

A static 10 KB stack (xTaskCreateStaticPinnedToCore) was built and measured, and not kept: static
data comes out of the heap region that fills first at start (0x3ffbef70, 368 B free at rest), so
10 KB more of the start-up allocations land in 0x3ffe4350 and the largest block at rest falls by
about as much; and the parse (13-15 KB for a full save) is the larger block anyway.

## R5 Budget

| | Before | After |
|---|---:|---:|
| Static data (.dram0.data + .bss) | 61,292 | 70,612 (ring 8.2 KB, the dispatcher's record 0.4 KB, trend 0.7 KB) |
| Kept on the heap at start | recent-alerts list 4.6 KB | none |
| MQTT read buffer | grown at run time, up to 4 KB | 4 KB at start |
| Net kept | | about +4.7 KB (+4 KB that was already held, now counted) |
| Flash | 90.9% | 90.9% |

## R6 A quiet automatic restart: not built

What is left after these changes is bounded by construction: the PCBs (16 at most, recycled), the
WebSocket clients (3 plus a refused one), the MQTT and N.I.N.A. connections, the alert queue (6,
room taken once). None grows with the session's length; each cut it makes is undone when its
connection ends. The trend (`largestLow15m` / `largestLow60m`) and the census's free map say
whether the board agrees: if the hour's soak drifts more than 12 KB with the free map naming the
same separator growing, the restart option comes back with that evidence (off by default, only
closed, idle and safe). Until then a restart a person didn't ask for, on a safety device, isn't
worth what it hides.
