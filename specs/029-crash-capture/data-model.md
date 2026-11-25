# Data Model: Crash Capture

All in `lib/CrashLogic` (namespace `SQM::Crash`), fixed-width fields only, so the same layout is
used in RTC memory, in NVS and in the native tests.

## Reset codes and kinds

`ResetCode` mirrors ESP-IDF's `esp_reset_reason_t` numbers (`src/CrashLog.cpp` checks them with
`static_assert`): 0 unknown, 1 power-on, 2 external, 3 software, 4 panic, 5 interrupt watchdog,
6 task watchdog, 7 other watchdog, 8 deep sleep, 9 brown-out, 10 SDIO, 11 USB, 12 JTAG, 13 eFuse,
14 power glitch, 15 CPU lock-up.

| Kind | From | Makes a record |
|---|---|---|
| `panic` | 4 | yes |
| `watchdog` | 5, 6, 7 | yes |
| `brownout` | 9 | yes |
| `none` | anything else | no |

`resetToken(code)`: `unknown`, `poweron`, `external`, `software`, `panic`, `watchdog` (5, 6, 7),
`deepsleep`, `brownout`, `sdio`, `usb`, `jtag`, `efuse`, `powerglitch`, `cpulockup` (SQMeter spec
038).

## Check value

`check(bytes, n)`: FNV-1a 32-bit. Each structure's `check` covers every byte before it.

## Capture (RTC)

| Field | Type | Notes |
|---|---|---|
| magic | u32 | `0xC2A5C001` when written |
| uptimeMs | u32 | at the panic |
| pc | u32 | |
| backtrace | u32[16] | |
| frames | u8 | 0-16 |
| core | u8 | |
| flags | u8 | bit 0 corrupt, bit 1 continues |
| reason | char[48] | NUL-terminated, cut |
| task | char[16] | |
| watchdogTasks | char[32] | task watchdog only |
| taskStackFree | u16 | the crashing task's stack left, read after a first seal, then sealed again |
| check | u32 | |

Cleared (magic 0) once stored as a record, and at a power-on.

## Snapshot (RTC)

| Field | Type | Notes |
|---|---|---|
| magic | u32 | `0xC2A5C002` |
| uptimeMs | u32 | |
| epoch | u32 | 0 while the clock isn't set |
| freeHeap, minFreeHeap, largestBlock | u32 | bytes |
| stackLoop, stackRoof, stackNetwork, stackPoll, stackAlerts | u16 | free bytes; `0xFFFF` unknown |
| roof | char[12] | the roof's state name |
| check | u32 | |

## Log line and ring (RTC)

`LogLine`: `ms` u32, `boot` u16, `level` u8 (0 debug … 4 fatal), `length` u8, `text` char[72],
`check` u32 (84 bytes). `LogRing`: `magic` u32 (`0xC2A5C003`), `boot` u16, `head` u16 (next slot),
`count` u16, pad, `check` u32 (over the header), `lines[50]`.

- `start(ring, keep)`: with `keep` and a valid header → `boot + 1`; else zero it, `boot = 1`.
- `push(ring, ms, level, text)`: cleans the text (drops ESC `[`…letter sequences and control
  characters, trailing spaces), cuts it to 71 characters, writes the slot, advances `head`,
  `count = min(count + 1, 50)`, re-checks the header.
- `copy(ring, out)`: the intact lines, oldest first.
- At `arm()`, before anything is logged, every intact line belongs to earlier runs; the record keeps
  the last 8 of them.

## Record (NVS blob, about 940 bytes)

| Field | Type | Notes |
|---|---|---|
| version, kind, resetCode, flags | u8 | `RECORD_VERSION` = 1; flags: bit 0 captured, 1 snapshot, 2 time known |
| seq | u32 | rises across restarts |
| uptimeMs | u32 | the capture's, else the snapshot's |
| epoch | u32 | snapshot epoch + (uptime − snapshot uptime) / 1000, when the snapshot had a clock |
| logCount | u8 | 0-8 |
| firmware | char[24] | firmware version |
| build | char[17] | first 16 hex digits of the `.elf` SHA-256 |
| capture | Capture | as read, when intact (frames capped at 16) |
| snapshot | Snapshot | as read, when intact |
| log | {u32 ms, u8 level, char text[72]}[8] | the last lines before this start |

- `build(inputs)`: from the reset code, the capture and snapshot (each only when its check holds),
  the firmware strings, the log lines (the last 8 are kept) and the next sequence.
- `slot(seq) = seq % KEEP` (`KEEP` = 3); `decode(bytes, n)` refuses another size, version or kind.

## Documents

- `crashesText(boot, records, n)`: "This start", "Running", "Records" lines, then each record after a
  blank line (`writeRecord`): `Record <seq>: <kind>`, `Time`, `Reset reason`, `Firmware`, `Reason`,
  `Task`, `Stuck tasks`, `Flash write`, `Backtrace`, `Memory`, `Stack left`, `Roof`, `Log before the
  restart`; lines without data are left out.
- `logText(lines, n)`: `#<boot> <s>.<ms>s <LEVEL> <text>` per line.
