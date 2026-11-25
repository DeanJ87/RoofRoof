# Contract: Crash records and the log

Both are plain text (`text/plain; charset=utf-8`), one fact per line, and need the password when
protection is on (`requireAuth`, `401`). Neither changes anything.

## GET /api/diag/crashes

```text
This start: panic (4), boot 3
Running: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Records: 2 (the last 3 are kept)

Record 2: panic
Time: 2026-11-25 02:14:07 UTC, 5231 s after start
Reset reason: panic (4)
Firmware: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Reason: LoadProhibited
Task: async_tcp, core 1, stack left 1904 B
Flash write: in progress
Backtrace: 0x400d5f2c 0x400d5f2c 0x400d61a0 0x40089e1d
Memory: free 61224 B, lowest 41020 B, largest block 31732 B
Stack left: loop 5980, roof 3416, async_tcp 6640, safetyPoll 2120, alerts 3876 B
Roof: closed
Log before the restart:
  5229.120s INFO  [Main] Settings saved
  5229.480s INFO  [Main] Settings saved

Record 1: brownout
Time: unknown (clock not set), 812 s after start
Reset reason: brownout (9)
Firmware: 0.1.0+dev, build 3f2a9c0e11d4b7a2
Reason: not captured
```

- The first three lines describe this start: its reset reason (SQMeter spec 038's names: `poweron`,
  `external`, `software`, `panic`, `watchdog`, `deepsleep`, `brownout`, `sdio`, `usb`, `jtag`,
  `efuse`, `powerglitch`, `cpulockup`, `unknown`) and code, the boot count, the running firmware and
  build, and how many records there are.
- Each record follows after a blank line, newest first, and starts `Record <seq>: <kind>` (`panic`,
  `watchdog` or `brownout`). A line the record has no data for is left out; without a capture the
  reason reads `not captured` and the task, stuck tasks, flash write and backtrace lines are left out;
  without a snapshot, the memory, stack and roof lines.
- `Firmware: <version>, build <16 hex>`: the build is the first 16 hex digits of the SHA-256 of that
  build's `firmware.elf` (`?` when unknown).
- `Task: <name>, core <n>, stack left <bytes|?> B`: the crashing task's stack left at the panic.
- `Backtrace: <pc> <frame> ...` with ` [corrupt]` and ` [continues]` when they apply: what
  `tools/decode_crash.py` reads.
- `Stack left: loop, roof, async_tcp, safetyPoll, alerts` (bytes, `?` unknown).

## GET /api/diag/log

```text
#2 5229.120s INFO  [Main] Settings saved
#3 0.412s INFO  [Main] === RoofRoof Starting ===
#3 0.431s WARN  [Crash] Restarted unexpectedly (panic): crash record 2 kept
```

`#<boot> <seconds>.<ms>s <LEVEL> <text>`, oldest first; `<boot>` counts the starts since the ring
was last emptied (a power-on); `<LEVEL>` is `DEBUG`, `INFO`, `WARN`, `ERROR` or `FATAL`.
