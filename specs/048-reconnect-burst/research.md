# Research: A Gradual Network Return

## R1 What the board showed (build d8e625f, spec 045; finding F51)

The census read at 12:00 (`shakedown/runs/heap-diag-1200.txt`) holds the per-minute trend of the
whole network batch (minutes 30-89 since start); each test's `telemetry.jsonl` has `/api/status`
at about 1 Hz. Matching the minutes to the tests by uptime:

| Test | Network event (uptime) | Minute: lowest largest / lowest free | 1 Hz samples after the return |
|---|---|---|---|
| F01b, access point down 120 s, roof open | down 2318 s, back 2439 s | 40: **3,316 B / 10,540 B** | +5 s free 47.3 KB, largest 38.9 KB; +7 s 40.0 / 23.5 KB; +10 s 31.8 / 8.2 KB; then **37 s with no answer** (the guard's "busy": free under 24 KB); +50 s 45.6 / 28.7 KB; +56 s 59-60 / 36.9 KB |
| F02, access point down 15 s into a close | down 3700 s, back 3746 s | 61-63: 19.4 / 12.8 / 12.8 KB | (the close ended in a fault) |
| F06, access point and N.I.N.A. PC down 121 s, weather unsafe | down 4255 s, back 4376 s | 73: **4,084 B / 9,588 B** | +11 s 34.8 / 51.6 KB; +13 s 11.8 / 24.5 KB; +23 s 19.4 / 28.8 KB; **18 s with no answer**; +42 s 31.7 / 58.2 KB |
| F07, the same, then N.I.N.A. back later | down 4888 s, back 4980 s | 83: 9,716 B / 22,440 B | back to 38.9 KB / 60 KB within a minute |
| F07, the open before the cut (not a return) | open at 4733 s | 78-80: 3,444 B, **1,012 B / 2,524 B free**, 3,956 B | free 58.3 → 48.0 → 44.4 → 23.4 KB in the first 4 s of the move, then 12 s with no answer, then the open stopped on "N.I.N.A. not responding: reply too slow or too long" |

- At rest between the tests the largest block was 32.7-38.9 KB with 58-62 KB free. A return took
  50-55 KB of that within 10 s and gave it back within a minute: transient allocations, not
  fragmentation (the free map at 12:00 shows the same 40.9 KB region block as at rest).
- The "no answer" stretches are the memory guard (`HeapSafe::admit`: largest under 6 KB or free
  under 24 KB answers everything but Alpaca and the roof's commands "busy"), and the census's own
  503s in the same seconds are its working memory not being had.
- The deepest point (1.0 KB largest, 2.5 KB free) was not a return: it was the start of an open,
  about five minutes after the N.I.N.A. PC came back, with N.I.N.A.'s park reads failing. A move
  doubles the live status rate (500 ms) and adds MQTT's move publishes; spec 045 R3b saw 17-24 KB
  dips during moves. This spec doesn't change a move; its records will say on the next run whether
  such a dip followed a return.
- 1 Hz samples miss the bottom of a dip that lasts less than a second; the minute's lowest is a
  sample too (one a second). The return records now sample ten times a second while a return is
  followed (FR-007).

## R2 What runs at a return

The access point coming back (the link up again, or, when an access point lost its uplink but kept
its clients, the requests after a silence):

| Who | What it allocates | Before | After (spec 048) |
|---|---|---|---|
| N.I.N.A. (several Alpaca devices, each polled) | per connection: PCB, `AsyncClient`, request object (~1 KB with its strings), a small reply, unacknowledged data | all at once, never capped (Alpaca is "essential"); retried connections arrive together | at most 3 open, past the first only with 16 KB in one block, for 30 s; 4 for the next 30 s |
| The harness's 1 Hz `GET /api/status`, a page's own reads | a built reply: 3 KB pool, 5 KB pieces, 2.9 KB send buffer, the request: about 12 KB (045 R3b) | 2 at once (spec 037) | 1 at once for 30 s, and inside the connection cap |
| Pages reconnecting their live status | the upgrade request, the client, the first 4.4 KB frame unacknowledged in lwIP | 3 per endpoint | the same, and their upgrades inside the connection cap |
| Safety polls (weather, then N.I.N.A.'s park) | `HTTPClient`, a 1.4 KB receive buffer, the body (N.I.N.A.'s mount info about 4 KB), the parse | at once, one after the other on their task | unchanged (never delayed: they decide the roof) |
| MQTT | the connect (the 4 KB read buffer is kept since 045), then all ~21 discovery configs in one pass: a 2 KB document and up to 5.7 KB unacknowledged | 3 s after the link | 10 s after a return, one attempt at a time, backing off; configs one per 200 ms, skipped when the broker kept them |
| Queued alerts | the record, the MQTT text, unacknowledged data (HTTPS: a TLS session, one at a time) | from 8 s, one a second | from 15 s, one per 5 s for the first minute |
| The close sequence, when the weather went stale during the outage | a close (500 ms live status, MQTT move publishes) | unchanged | unchanged |

The WiFi driver's dynamic receive and transmit buffers (up to 32 each, 1.6 KB, while traffic is in
flight) and lwIP's segments also peak in those seconds; they are set by the Arduino core at run time
and not touched here (038 R4).

A station that loses its access point is disconnected by ESP-IDF, which clears its IPv4 address
(`esp_netif_down_api`); lwIP then aborts every TCP connection bound to it
(`tcp_netif_ip_addr_changed`), so the connections of before the outage are gone when it comes back.
An access point that only loses its uplink may keep its clients associated: the link never drops,
nothing is aborted, and the device sees only that no one connects. That is why a return is also
taken from the requests (`RequestsBack`, 30 s of none).

## R3 A model of a return (test_return_burst)

The same arrivals run twice at 10 ms steps over the 90 s after a return: four N.I.N.A. devices
retrying at once then polled every 2 s, the harness's 1 Hz status, two pages reconnecting with a
status read each, the safety polls every 5 s, MQTT with the worst-case discovery (the broker kept
nothing), six queued alerts. Each piece holds an upper bound of its bytes for its time (the table
above). The 038 schedule holds **46.3 KB** at once at its peak (one connection refused at accept);
spec 048's, calling the real rules, holds **29.4 KB** (67 connections answered busy over the first
minute, all retried and served), MQTT at 10.4 s, discovery done by 14.9 s, all six alerts sent.
The model has no WiFi buffers and no retransmissions, so it reads lower than the board (about 55 KB
taken); it is a check on the order and overlap of the work, kept as a regression test. The board
check is the soak (SC-001).

Tried in the model and not kept: two connections in the window (28.9 KB, 102 refused: a third of
N.I.N.A.'s requests turned away for little gain); a window that ends at once (the retries held back
all land together at 30 s: 31.3 KB, hence the second step of 4 connections to 60 s).

## R4 Why at accept, and the ticket

`AsyncWebServerRequest::create` allocates the request (about 1 KB with its strings and shared
pointer) as soon as a connection is taken; the memory guard (spec 031) runs only once the request
line and headers are parsed, after that. The device owns the listener (`Ipv6Network::listen`), so
the gate runs before the request exists: a refused connection costs one 164-byte segment and its
PCB until the peer's FIN.

The count is of request objects: `request->onDisconnect` holds a copy of an `InboundGate::Ticket`
(copies share one slot of a `HeapSafe::ReplyLedger`), released when the request is destroyed (its
disconnect, or a WebSocket upgrade, which hands the client over and frees the request). A slot
older than 10 s no longer counts, so a request that is never freed can't hold the gate shut (fix
041's lesson). An upload replaces the disconnect handler: it is no longer counted.

## R5 The discovery marker

Home Assistant keeps entity configs as retained messages on the broker; Mosquitto keeps retained
messages across its restarts when persistence is on (Home Assistant's add-on default). Sending the
~21 configs again after every reconnect is only needed when the broker lost them. After the last
config the device publishes `<base>/discovery` (retained) with an FNV-1a hash of everything the
configs depend on (8 hex digits). On a later connection in the same boot it subscribes, waits up to
2 s for the retained marker, and sends nothing when it matches. The first connection after a
restart always sends the configs (so a restart repairs configs deleted by hand), as does any change
of the settings they depend on.

## R6 Budget

| | Before | After |
|---|---:|---:|
| Static data | +0 | `NetworkReturn` (8 records, about 340 B), the gate's ledger and counts (about 50 B), the census's copy is in its working memory |
| Task stacks | unchanged | unchanged |
| Per paced discovery step | (one 2 KB document for all) | one 2 KB document, freed after the config |
