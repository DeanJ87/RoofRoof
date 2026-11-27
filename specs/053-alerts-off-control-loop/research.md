# Research: Alerts Off the Control Loop

## R1 What the field records say

Crash records 44 and 45 (`GET /api/diag/crashes`, firmware f6595b9) are both reset reason 5,
`ESP_RST_INT_WDT`: the **interrupt** watchdog (`CONFIG_ESP_INT_WDT_TIMEOUT_MS=300`), not the task
watchdog (30 s, `esp_task_wdt_reconfigure` in `setup`). It fires when a core's tick interrupt hasn't
run for 300 ms: interrupts off on that core (a critical section, a spinlock another core holds, a
flash operation) for that long. F56's "Control step 344 ms late" is the same kind of stall seen from
the roof task: it couldn't run for 344 ms, just past the 300 ms the interrupt watchdog allows.

- Record 44: "Imaging app stopped checking" sent at 124 s; "AlertQueue: Low memory (largest block
  5364 B): alerts wait" at 172 s; a network write stopped; MQTT reconnected; restart.
- Record 45: MQTT reconnecting in a loop, then "Reached the closed magnet ... State closing ->
  closed", then the restart.
- The roof task (priority 5, core 1) can't be held up by the alert task's CPU time (priority 1); it
  can by a stall of its core's interrupts or a lock it needs. What held them for 300 ms on a starved
  heap wasn't captured ("Reason: not captured"). Common to both records: a TLS alert path on a heap
  whose largest block had fallen to about 5 KB.

So this spec removes the conditions the records share rather than one proven call path: no TLS
session on a short heap (it took the rest of the device's memory), no TLS work on the roof's core,
no TLS session during a move. The owner's bench check (spec.md) is the evidence it worked.

## R2 Why not a static task and stack

A permanent task with a static 8 KB stack would avoid creating the task on demand, but takes 8 KB
of DRAM for good, and the heap at rest is the constraint (38-47 KB largest block). Spec 038 kept the
on-demand task for that reason; the hand-over (the queue) is what must not wait, and it doesn't.

## R3 Other ways the roof task can stall (not in this spec)

- **Serial logging from the roof task.** `RoofControl::logChanges` runs between steps and prints over
  the UART at 115200 baud (about 11 bytes a millisecond) behind any other task's lines. A burst of
  log lines elsewhere (MQTT reconnecting every 15 ms, as in record 45) can hold the roof task's own
  line, and the gap between steps grows by that. Moving its lines to a queue drained by the main
  loop would bound it; a follow-up if the bench still shows late steps.
- **Heap walks.** `heap_caps_get_largest_free_block` walks every block under the heap's lock; the
  census already refuses to run during a move (spec 038).
- **Flash writes** stall both cores; they are held while the roof moves (spec 010).

## R4 The TLS floor

mbedTLS here: `CONFIG_MBEDTLS_DYNAMIC_BUFFER`, 16 KB in / 4 KB out (asymmetric). The largest single
allocation is the 16.4 KB record buffer; the session totals about 25-40 KB (spec 038's budget). The
floor asks for a 44 KiB block (the session can come out of one region: TLSF places it there and
leaves the small scattered blocks alone) and 56 KiB free (40 KB session + 16 KB left, the spec 037
soak's free-heap floor). Spec 045's readings show the largest block's reports sit at class
boundaries (a 45 KB block reports about 46,068 B), so 45,056 B passes a 45 KB block.

The live board on main 6275f8a (2026-11-27): free 59.5 KB, largest 47.1 KB, lowest largest 23.5 KB
over 15 minutes. Most of the time the floor holds; in the dips the alert waits, which is the point.
Smaller mbedTLS buffers (`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN`, max fragment length) would lower the
floor but need their own soak against Pushover and ntfy.sh: not in this spec.

## R5 Stack

`tools/firmware/stack_depth.py` (extended): from `AlertDispatcher::taskEntry` (run, deliver,
deliverOne and sendLocked are inlined into it) through `sendMeasured`, `post`, `httpPost`,
`HTTPClient::sendRequest` and `connect`, into `NetworkClientSecure::connect` (reached through the
vtable, so a stage) and the handshake: deepest 5,792 B (`ssl_starttls_handshake` ->
`mbedtls_ssl_handshake` -> certificate verify -> RSA-PSS -> SHA-512, 800 B), with the 1 KB margin
6,816 B of 8,192 B. Calls through pointers inside mbedTLS aren't followed, so the task also logs its
least-left when it stops; the owner's bench gives the measured figure.
