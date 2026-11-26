# Heap budget

The ESP32 has two heaps that matter here. `heap8Bit` in `/api/status` is the one `malloc` and `new`
use (byte-addressable RAM); `freeHeap` and `maxAllocHeap` also count about 59 KB of IRAM that
ordinary allocations never get. A request, a WebSocket frame or a TLS session needs a single free
block, so the **largest free block** is the number that decides whether the device can answer.

Spec 038 measured where that heap goes and cut what was never used. This page is the budget: keep
it current when a change adds a task, a kept buffer or static data.

## Measuring

- `GET /api/diag/heap` (REST reference): totals, regions, blocks by size, the 16 largest blocks in
  use with task stacks named, and every task's stack with the least it has had left.
- `/api/status` `heapStages`: free memory after each start-up stage (`free` counts IRAM too;
  `largest` is the 8-bit heap).
- `tools/soak/ws_soak.py`: at rest and under load, with floors (below).
- `tools/firmware/heap_budget.py`: what the firmware keeps for good, from the sources and the
  `.elf` (CI, after the build).
- Spec 045: `/api/status` `heap8Bit.largestLow15m` / `largestLow60m`, and in the census the last
  hour minute by minute (`Trend`) and the free blocks of 1 KB or more with what is in use between
  them (`Free blocks ...`): what cut the largest block apart.

## The budget

Kept for the device's lifetime (bytes):

| Consumer | Before 038 | After 038 | Notes |
|---|---:|---:|---|
| Static data (`.dram0.data` + `.dram0.bss`) | 61,212 | 61,228 | the roof's state, timer interrupt stack, WiFi |
| loop task stack | 12,288 | 10,240 | measured peak about 7.3 KB |
| `async_tcp` task stack | 11,264 | 8,192 | measured peak about 5 KB |
| `esp_timer` task stack | 8,192 | 4,096 | motor watchdog, restart timer, WiFi timers |
| `safetyPoll` task stack | 8,192 | 8,192 | room for an https SQMeter's TLS handshake |
| roof task stack | 6,144 | 6,144 | measured peak about 3.3 KB |
| Frame pool (spec 037) | 10,240 | 10,240 | 2 x 5 KB live-update frames |
| MQTT buffer | 3,072 | 3,072 | the largest MQTT document |
| Emergency reserve | 0 | 2,048 | given up instead of `abort()` |
| MQTT read buffer (spec 045) | grown to up to 4,096 at run time | 4,096 at start | `DualStackClient::rx`, taken whole when MQTT starts |
| Recent alerts (spec 045) | 4,620 on the heap + 2 strings per alert | 8,200 static | 20 fixed records: title 64, message 192, detail 32 per channel |
| WiFi static RX buffers | 6,400 | 6,400 | 4 x 1.6 KB, set by the Arduino core at run time |
| **Sum of the above** | **127,004** | **119,852** | **-7.2 KB** |

On demand (taken while in use, then freed):

| Consumer | Before 038 | After 038 | Notes |
|---|---:|---:|---|
| `alerts` task stack | 8,192 | 8,192 | started for an HTTPS alert, exits when idle |
| `config_save` task stack (fix 041) | - | 10,240 | every web settings save, while it runs (was on `async_tcp`) |
| `backup_job` task stack | 8,192 | 10,240 (fix 041) | a backup or a restore preview |
| TLS session (alert, https SQMeter) | about 25-40 KB | same | dynamic buffers, 16 KB in / 4 KB out at most |
| Unsent data per connection | up to 5,744 | up to 5,744 | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT`; halving it stalled MQTT after a network return |
| Status reply | 3 KB pool + 1 KB pieces | same | at most 2 replies in flight (spec 037) |
| `GET /api/config` | 2 x 9.7 KB | same | behind the memory guard |
| WiFi dynamic RX/TX buffers | up to 32 each | same | 1.6 KB each, while traffic is in flight |

On the board:

| Reading | Before 038 (main) | After 038 |
|---|---:|---:|
| Fresh boot (uptime 7 s), `heap8Bit` free / largest | 64.8 KB / 38.9 KB | 62.6 KB / 34.8 KB |
| At rest after 3-4 min, 3 pages open: free / largest | 45-56 KB / 13-37 KB | 67.6-69.3 KB / 57.3-61.4 KB |
| `heapStages` "setup complete" largest | 69,620 B | 77,812 B |
| Least stack left: loop / `async_tcp` / `esp_timer` | 4,996 / 6,264 / - B | 2,940 / 3,492 / 3,768 B |
| Network drop and return at rest, 3 pages: lowest free since start | 352 B | 1,768 B (no restart, reserve not needed, 20 replies busy) |
| Spec 037 soak, 10 min: largest block lowest | 10.2 KB | bench (pending) |
| Spec 037 soak: free heap lowest | 21.1 KB | bench (pending) |

## Floors

| Check | Floor | Where |
|---|---|---|
| Kept for good (stacks, buffers, static data) | `tools/firmware/heap-budget.json` | CI, after the firmware build |
| A settings save's deepest path, with 1 KB to spare, within its task's stack; never on `async_tcp` | `tools/firmware/stack_depth.py` | CI, after the firmware build |
| Largest block at rest | 40 KB | `ws_soak.py --clients 0 --also --minutes 3 --floor 40960` |
| Spec 037 soak, 10 minutes | largest 20 KB, free 15 KB | `ws_soak.py --clients 4 --floor 20480 --free-floor 15360 --also ...` |

To record growth on purpose: `python3 tools/firmware/heap_budget.py --update-baseline --reason "..."`
after a build, and update the tables here.

## Over a session (spec 045)

The free heap can stay the same while the largest block falls: something made at run time and
kept lands inside the largest free block and cuts it. On main e3a09a4 the region holding the
largest block went from 63 KB largest to 38.7 KB within 15 minutes of a page being open on the
dashboard and stayed there with the page closed (free unchanged, about 50 more blocks in use than
at start). The rules since spec 045:

- **Nothing kept is made at run time when it can be kept from the start**: live frames are written
  in place in the frame pool, the recent alerts are static records, the MQTT read buffer is taken
  whole when MQTT starts, the WebSocket bookkeeping is copied onto the stack.
- **A page costs one file once**: the hashed `/assets/` files are cached by the browser for a year
  (a new UI has new names); a reload fetches `index.html` only. Without the cache a page load was
  six streams of about 10 KB each at once.
- **A page asks once**: the banners read the live status the page already receives, not
  `GET /api/status` every 5-10 s.
- **A memory rule asks for what the work needs**, measured (`measureJson`), not a fixed size: the
  heap reports its largest block at class boundaries (20,468 B for a 20 KB block), so "at least 20
  KB" refused every settings save.

On the bench, beside the harness's load for an hour:

```bash
python3 tools/soak/ws_soak.py --host 192.168.1.245 --clients 2 --minutes 60 --page-load 30 \
    --also /api/status@1 "/api/v1/dome/0/shutterstatus?ClientID=1&ClientTransactionID=1@1" \
    --floor 16384 --max-drift 12288
```

`largestDrift` is how far the best largest block of the last 5 samples fell below that of the
first 5; a pass keeps it within 12 KB. Read `GET /api/diag/heap` at the end: its free-block map
names what sits between the large free blocks.

There is no automatic restart for fragmentation (spec 045 research R6): what is still made at run
time is bounded (16 TCP connections, 3 pages, the queues' fixed room) and freed when its connection
ends.

## When the heap runs out anyway

Checked allocations (`new (std::nothrow)`, `malloc`, the reply pieces) fail softly: the request is
answered `503` busy. The throwing `operator new` (strings, lists and callbacks inside libraries)
can't fail softly without exceptions, so the firmware keeps a 2 KB reserve: the C++ new-handler
frees it and the allocation is retried. While it is spent the memory guard answers non-essential
requests busy; the main loop takes it back once the largest block is 8 KB again and logs the use.
`heap8Bit.reserveSpent` counts the uses since start.

## After a network outage

Everything that lost its connection used to reconnect in the same second: MQTT and its Home
Assistant discovery documents, the queued alerts with their TLS sessions, the safety polls, the
clock. On main that took the heap's lowest point to 352 B. Now the safety polls resume at once,
MQTT waits until the network has been up 3 s and the alert outbox 8 s
(`ConnectionBudget::NetworkSettle`). Check it on the bench by blocking the roof's access point for
2 minutes while `ws_soak.py` samples, then reading `heap8Bit.minFree`.

## Stack depth of a settings save (fix 041)

Spec 038 sized `async_tcp` from the peaks the crash snapshots had recorded; a settings save wasn't
among them, and on the board it overflowed the 8 KB stack (stack left 0-8 B, in the analog check's
`snprintf` of a float, under `Config::applyJson`'s 3 KB frame). Saves now run on their own task
(`include/SaveTask.h`), and `tools/firmware/stack_depth.py` reads the build: each function's frame
from its `entry` instruction, its direct calls, and the deepest path from the save's stages. On the
fix's build: deepest 6,288 B of the save task's 10,240 B, and 7,216 B of the backup task's 10,240 B.
It is an upper bound (it takes the worst branch everywhere, a malloc's own path included).

On the bench, after a save: the log line "Config save: stack low-water N B on the save task, M B on
async_tcp" gives what was left on both; N should stay above 2 KB.

