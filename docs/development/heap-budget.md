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
| WiFi static RX buffers | 6,400 | 6,400 | 4 x 1.6 KB, set by the Arduino core at run time |
| **Sum of the above** | **127,004** | **119,852** | **-7.2 KB** |

On demand (taken while in use, then freed):

| Consumer | Before 038 | After 038 | Notes |
|---|---:|---:|---|
| `alerts` task stack | 8,192 | 8,192 | started for an HTTPS alert, exits when idle |
| TLS session (alert, https SQMeter) | about 25-40 KB | same | dynamic buffers, 16 KB in / 4 KB out at most |
| Unsent data per connection | up to 5,744 | up to 2,872 | `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` |
| Status reply | 3 KB pool + 1 KB pieces | same | at most 2 replies in flight (spec 037) |
| `GET /api/config` | 2 x 9.7 KB | same | behind the memory guard |
| WiFi dynamic RX/TX buffers | up to 32 each | same | 1.6 KB each, while traffic is in flight |

On the board:

| Reading | Before 038 (main) | After 038 |
|---|---:|---:|
| Fresh boot, `heap8Bit` free / largest | 64.8 KB / 38.9 KB | bench (pending) |
| At rest, largest block | 21-37 KB | bench (pending) |
| Spec 037 soak, 10 min: largest block lowest | 10.2 KB | bench (pending) |
| Spec 037 soak: free heap lowest | 21.1 KB | bench (pending) |
| Spec 037 soak: lowest free since start | 584 B | bench (pending) |

## Floors

| Check | Floor | Where |
|---|---|---|
| Kept for good (stacks, buffers, static data) | `tools/firmware/heap-budget.json` | CI, after the firmware build |
| Largest block at rest | 40 KB | `ws_soak.py --clients 0 --also --minutes 3 --floor 40960` |
| Spec 037 soak, 10 minutes | largest 20 KB, free 15 KB | `ws_soak.py --clients 4 --floor 20480 --free-floor 15360 --also ...` |

To record growth on purpose: `python3 tools/firmware/heap_budget.py --update-baseline --reason "..."`
after a build, and update the tables here.

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
