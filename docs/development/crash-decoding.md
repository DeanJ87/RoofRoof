---
description: "How RoofRoof records a crash without a serial cable (RTC memory, the panic hook, NVS records) and how to turn a record's addresses into code with tools/decode_crash.py and the build's .elf."
---

# Decoding a crash

An installed controller has no serial cable, so the panic message and backtrace that a serial
monitor would show are kept by the device itself ([Crashes](../user-guide/diagnostics.md#crashes),
spec 029). This page is about turning a record into code and about how the capture works.

## Decode a record

1. Get the record: **Copy** on the Diagnostics page's Crashes card, or

    ```bash
    curl -s -u admin:PASSWORD http://roofroof.local/api/diag/crashes > crash.txt
    ```

2. Get the `firmware.elf` of **the same build**. The record's `build` is the first 16 hex digits of
   that file's SHA-256:
    - a CI build: the `firmware-elf` artifact of the run (kept 5 days for a pull request, 90 for a tag);
    - a release: `roofroof-l2-firmware-<tag>.elf.gz` among the release's files;
    - a local build: `.pio/elf/<build>.elf.gz` (the last 5 builds are kept; `scripts/keep_elf.py`).

3. Decode:

    ```bash
    python3 tools/decode_crash.py crash.txt --elf .pio/elf/3f2a9c0e11d4b7a2.elf.gz
    ```

    ```text
    Record 2: panic | LoadProhibited | task async_tcp, core 1, stack left 1904 B | firmware 0.1.0+dev
      0x400d5f2c: <function> at <file>:<line>
      0x400d61a0: <function> at <file>:<line>
    ```

The tool takes the whole document, one copied record, or `-` for stdin; `--addrs` takes plain addresses
(also the serial monitor's `0x400d5f2c:0x3ffb1230` pairs). It refuses an `.elf` whose SHA-256
doesn't match the record's `build` (the names would be wrong); `--force` decodes anyway with a
warning. It uses `xtensa-esp32-elf-addr2line` from the `PATH` or from PlatformIO's toolchain
(`~/.platformio/packages/toolchain-xtensa-esp-elf/bin`), or `--addr2line`.

## How the capture works

| Part | Where | Written by | Survives |
|---|---|---|---|
| Capture: reason, PC, 16 frames, task, core, task watchdog's stuck tasks | RTC slow memory (`RTC_NOINIT`) | the panic hook | a panic restart |
| Snapshot: uptime, time, heap, stacks, roof state | RTC slow memory | the main loop, once a second | a software or panic restart; a brown-out if intact |
| Log ring: 50 lines of 71 characters | RTC slow memory | `Logger` (the Arduino core's and ESP-IDF's own lines go to the serial port only) | the same |
| Records: the last 3 | NVS namespace `crashlog` | `CrashLog::keepRecord()` at start | power cuts |

- **The panic hook** is the Arduino core's: `set_arduino_panic_handler()` is called by the core's
  `__wrap_esp_panic_handler`, which walks the backtrace and then hands over to ESP-IDF's own
  handler, which prints and restarts as before. The wrap needs `-Wl,--wrap=esp_panic_handler`
  (`platformio.ini`); the Arduino IDE passes it, PlatformIO's builder doesn't. The hook only copies:
  no allocation, lock, flash or log.
- **A flash write in progress** at the panic is noted (`flashWrite`), from `FlashGuard`'s lock-free
  mirror. The firmware registers no interrupt of its own (`tools/firmware/test_isr_audit.py` keeps
  it so): an interrupt running with the flash cache off is the usual cause of "Cache disabled but
  cached memory region accessed".
- **The task watchdog** calls `esp_task_wdt_isr_user_handler()` before its panic; RoofRoof's version
  asks `esp_task_wdt_print_triggered_tasks()` for the stuck tasks' names.
- **A brown-out** and the RTC watchdog have no hook: their record is the reset reason and the last
  snapshot (`captured: false`).
- **Each RTC part** carries a magic and an FNV-1a check; after a power-on or a brown-out that
  garbled it, it is ignored (`lib/CrashLogic`).
- **The record** is written once, in `setup()` after the motor driver's EN is low and before the
  roof's task starts, so the flash write can't wait behind a move (spec 010's rule).
- **Why not a core dump**: the partition table (`partitions.csv`, layout `l2`) fills the 4 MB chip
  and can only change over USB; a core dump also writes flash from the panic handler
  (`specs/029-crash-capture/research.md`, R1).

## The build id

ESP-IDF writes the `.elf`'s SHA-256 into the image's app descriptor (`esptool elf2image
--elf-sha256-offset 0xb0`; pioarduino's hybrid compile passes it), and the firmware reads it with
`esp_app_get_elf_sha256()`. `scripts/keep_elf.py` checks after every build that the 32 bytes at
`0xb0` of `firmware.bin` are the SHA-256 of `firmware.elf`, and fails the build if not.
