# Research: Crash Capture

## R1. Core dump partition or RTC memory

- **Decision**: RTC slow memory that isn't initialised at start (`RTC_NOINIT_ATTR`), copied to NVS at
  the next start.
- **Rationale**: `partitions.csv` (layout `l2`) fills the 4 MB chip: nvs 0x9000 (20 KB), otadata,
  app0 and app1 at 0x1C0000 (1.75 MB) each, LittleFS 0x70000 (448 KB), ending at 0x400000. A
  core dump partition (64 KB or more) needs a smaller slot or LittleFS, and the table only changes
  by USB (a device updated over the air keeps its old table). `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`
  stays. RTC slow memory on the ESP32 has 0x2000 − 512 − 24 bytes for data; the build uses 124 of
  them (`.rtc_noinit` 88, `.rtc.force_slow` 32, `.rtc.data` 4), so about 7.5 KB are free.
  `RTC_NOINIT` memory keeps its content across a software reset and a panic restart, and is
  random after a power-on. After a brown-out it may be kept or not, so every part carries a check.
- **Alternatives**: a core dump to flash (layout change, USB only, 64 KB flash write in the panic
  path); a core dump to UART (needs the cable that isn't there); `.noinit` DRAM (also survives a
  software reset, but costs heap).

## R2. How the panic is caught

- **Decision**: the Arduino core's hook: `set_arduino_panic_handler(handler, arg)` (esp32-hal.h).
  Its `__wrap_esp_panic_handler` (esp32-hal-misc.c) walks the backtrace (up to 60 frames, with
  corrupt and continues flags), calls the handler with `arduino_panic_info_t` (core, reason, pc,
  backtrace), then calls ESP-IDF's own handler, which prints and restarts as before. The wrap needs
  `-Wl,--wrap=esp_panic_handler` at link time. The Arduino IDE passes it (platform.txt); the
  pioarduino builder does not, and the linked image showed no `__wrap_esp_panic_handler`: the
  flag goes into `build_flags`.
- **Rationale**: no copy of IDF internals (`panic_info_t` is private), no second wrapper, the core's
  backtrace walker is the one the serial decoder's output comes from. `esp_panic_handler` is in
  flash in this build (`0x402247b4`), so the hook can be in flash too.
- **What the hook may do**: copy fields and strings into the RTC capture, read the current task's
  name for the core (`pcTaskGetName(xTaskGetCurrentTaskHandleForCore(core))`, no lock), read the
  uptime (`esp_timer_get_time`, no lock), compute the check. Not: allocate, lock, log, read the
  clock (`time()` may take a lock) or read heap figures (the heap may be what is broken).
- **Alternatives**: our own `--wrap` of `esp_panic_handler` (conflicts with the core's);
  `esp_register_shutdown_handler` (not called on a panic).

## R3. Watchdogs and brown-out

- Task watchdog: `trigger_panic = true` (main.cpp), so a timeout ends in the panic hook with the
  reason "Task watchdog got triggered…". Before that, ESP-IDF calls the weak
  `esp_task_wdt_isr_user_handler()`; it calls `esp_task_wdt_print_triggered_tasks(handler, …)`,
  which hands the names of the tasks that didn't reset the watchdog to our handler, which copies
  them into the capture (up to 31 characters). The reset reason reads `ESP_RST_TASK_WDT`.
- Interrupt watchdog: a panic ("Interrupt wdt timeout on CPU0"), reset reason `ESP_RST_INT_WDT`.
- RTC watchdog and other watchdogs (`ESP_RST_WDT`) and brown-out (`ESP_RST_BROWNOUT`): no hook. The
  record is the reset reason plus the last snapshot (at most about a second old), marked "no
  capture".
- The ESP32 also reports a slow power-up as a brown-out (SQMeter spec 038); the wording says so.

## R4. Matching a record to its `.elf`

- **Decision**: esptool's `elf2image --elf-sha256-offset 0xb0` writes the `.elf`'s SHA-256 into
  `esp_app_desc_t.app_elf_sha256` (image header 0x18 + segment header 0x8 = 0x20, plus the field's
  offset 0x90 in the descriptor), as ESP-IDF's own build does. pioarduino's hybrid compile (used
  here because of `custom_sdkconfig`) already passes it (`builder/frameworks/espidf.py`,
  `extra_elf2bin_flags`): the baseline image holds the `.elf`'s SHA-256 at 0xb0. Nothing makes a
  plain Arduino build do the same, so `scripts/keep_elf.py` (a post-build step) checks it after
  every build and fails the build when the bytes don't match, so a builder change can't silently
  break the match. The firmware reads it with `esp_app_get_elf_sha256` and keeps the first 16 hex
  digits in each record. `decode_crash.py` hashes the given `.elf` and compares.
- **Check**: after a build, the 4 bytes at 0x20 of `firmware.bin` are the descriptor's magic
  `0xABCD5432` and the 32 bytes at 0xb0 equal `sha256(firmware.elf)` (quickstart §2).
- **Keeping the `.elf`**: CI uploads it as the `firmware-elf` artifact (5 days for a PR, 90 for a
  tag); the release attaches `roofroof-l2-firmware-<tag>.elf.gz`. A local build keeps the last 5
  gzipped under `.pio/elf/<16 hex>.elf.gz` (about 10 MB each), so a board flashed from a local
  build can still be decoded.

## R5. Log ring

- **Decision**: 50 lines of 84 bytes (time since boot, boot number, level, length, 72 bytes of
  text, check) in RTC memory, behind a header with a magic, the boot number, the head, the count
  and a check. Written by `Logger::log` (after its print). ESP-IDF's own lines aren't hooked (R9). A `portMUX` spinlock with
  `portENTER_CRITICAL_SAFE` guards the copy of one line; formatting happens before, on the
  caller's stack (72 bytes).
- **At start**: after a power-on the ring is emptied; otherwise it is kept when the header's magic
  and check hold and the head and count are in range, and the boot number goes up by one. Lines
  whose check fails are skipped when read.
- **Alternatives**: a heap buffer (lost at the restart, costs heap); LittleFS (flash wear, and a
  write from any task).

## R6. Records in NVS

- **Decision**: namespace `crashlog`; `seq` (u32, the last sequence used) and blobs `r0`..`r2`; a
  record goes to slot `seq % 3`. The blob is the `Crash::Record` struct (fixed-width fields, a
  version byte first, `static_assert` on its size), rejected when its size or version differs.
- **Size**: about 940 bytes per record, 3 records ≈ 2.8 KB ≈ 95 NVS entries; the bench device reports
  342 free of 630.
- **When**: `setup()`, after `PinSetup::begin` and before `roofControl.begin`. The roof can't move
  before its task starts, so spec 010's flash-write rule holds without the platform guard.
- **Reading**: `GET /api/diag/crashes` reads the three blobs on the request (NVS has its own lock;
  a read of 2.8 KB is like serving a file from LittleFS on the same task), straight into heap
  buffers (not through `Preferences::getBytes`, which copies through the stack), and sends the text.

## R7, R8. The restart alert, MQTT and Home Assistant (withdrawn)

- A first version added a "Restarted unexpectedly" alert event (once per record, after the start-up
  grace, counted across a crash loop, its announced sequence kept in RTC memory), a retained
  `<base>/device` message in SQMeter spec 038's shape and a "Last restart reason" diagnostic sensor.
  They cost about 5 KB of flash and are withdrawn (R9); a follow-up can add them once there is room.

## R9. Flash and memory

- Main before this spec (with spec 027): `firmware.bin` 1,728,496 bytes, 94.2% of the 1,835,008-byte
  slot; the gate fails above 95% (1,743,257 bytes), and spec 030 is adding code too.
- The first version (JSON through ArduinoJson, chunked replies, the alert, MQTT, Home Assistant, an
  ESP-IDF log hook) grew the image by about 18.6 KB. Cut back to: the capture and snapshot, the ring
  fed by the logger only, records that keep the capture and snapshot as read (a few copies instead of
  a field-by-field translation), the last lines taken at `arm()` (no separate tail walk), records
  read newest-first by sequence (no sort), and two hand-written plain-text documents sent whole.
  Measured growth: see the PR (about 7.5 KB). What remains is the record and its text (~1 KB), the
  hooks and the Arduino core's backtrace walker it pulls in (~0.8 KB), the ring (~0.6 KB), the
  snapshot, the NVS store and the two routes.
- RTC: about 4.5 KB more of RTC slow memory; no heap at rest; a request takes a few KB of heap while
  it runs (the records or the ring copy and the text).

## R10. Field evidence: panics while the Hall input flapped

- **Report**: the bench board (main f3c98f6) panicked (`ESP_RST_PANIC`, not a brown-out) four
  times within about a minute while a loose Hall connector made the reading jump between 22 and
  2066 at rest (the `hallRail` fault latching), at uptimes of about 7, 7, 53 and 10 s; earlier the
  same day once as a move arrived at the open end and twice during settings saves.
- **The fault store path**: `test/test_hall_flap` drives the controller with a rail and a mid reading
  alternating every step (and every 37 steps) for 1000 s, and with a person clearing the fault every
  2 s, counting the NVS writes `RoofControl::persistFault` would make (it writes only when the kept
  record changes). The latch is sticky: one latch and one write without a clear; with clears, at
  most a write for the fault and one for its removal per cycle; the driver stays off throughout; the
  record reads back as written. So a flapping input doesn't turn into a stream of flash writes from
  the roof task, and NVS writes from different tasks are serialised by NVS's own lock.
- **The roof task's stack**: 6144 bytes. Its deepest static frames (`-fstack-usage` on this build):
  `snapshotWithChecks` 576 B (not on the task), `logChanges` 400, `refreshSnapshot` 336, `checkGates`
  240, `readInputs` 224, `logFaultHall` 128, plus the `printf` the logger calls on a fault. The status
  showed 3.2-3.5 KB left after faults had latched and been logged, so the fault path already ran
  inside the recorded high-water mark; the logger's new ring copy adds one 72-byte buffer and a
  `vsnprintf` that runs before, not inside, the `vprintf`. Nothing points to the roof task's stack,
  but the poller and the alert sender (TLS, 8 KB each) weren't measured at all.
- **What this spec adds for the next one**: the record names the task (and core) that crashed, the
  stack that task had left at the panic, and the last snapshot's stack headroom of all five tasks
  (loop, roof, web server, weather poller, alert sender), plus the log lines before it, so the next
  panic says which of these it was. The cause itself stays open until a record arrives.

## R11. Field evidence: every panic during a flash write?

- **Report**: each panic so far came with a flash write: settings saves (NVS), the `hallRail`
  fault's record (NVS, from the roof task) and a move's end (the kept move summaries, LittleFS).
  The classic ESP32 failure for that pattern is "Cache disabled but cached memory region accessed":
  an interrupt that keeps running while the flash cache is off, whose code or data is in flash.
- **Audit** (this branch, `src/`, `include/`, `lib/` and the libraries in `.pio/libdeps`):
  - no `attachInterrupt`, `gpio_isr_handler_add`, `esp_intr_alloc`, `ESP_INTR_FLAG_IRAM`,
    `IRAM_ATTR` or hardware-timer interrupt anywhere; the park switches, the Hall sensor, the supply
    and the current sense are polled by the roof's control task (`analogRead`, `digitalRead`);
  - both `esp_timer`s (the roof's motion watchdog, the restart timer) use `ESP_TIMER_TASK`, so their
    callbacks run in the timer task, not an interrupt; `CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD`
    is off;
  - LEDC (the motor's PWM) has no interrupt; AsyncTCP, ESPAsyncWebServer and PubSubClient register
    none;
  - the task watchdog's interrupt (which calls this spec's `esp_task_wdt_isr_user_handler`) is
    allocated with flags 0, not IRAM, so ESP-IDF masks it during a flash operation like every other
    non-IRAM interrupt (`CONFIG_SPI_FLASH_PLACE_FUNCTIONS_IN_IRAM=y`, the other core is parked in
    IRAM for the duration).
  So no interrupt of RoofRoof's can run while the cache is off; the drivers' own IRAM interrupts
  (Wi-Fi, esp_timer's) are ESP-IDF's. `tools/firmware/test_isr_audit.py` (CI) now fails if one is
  added without being listed with the reason it is safe.
- **Concurrent writes**: NVS (settings from the web server's task, the fault record from the roof
  task, alert state from the main loop) and LittleFS (kept move summaries from the main loop) both
  go through `spi_flash`, which takes one OS lock per operation and parks the other core, so two
  writers queue rather than overlap; NVS also has its own lock per partition. Writes from the roof
  task are short (one 16-byte blob) and never while it drives (`persistFault`).
- **Not ruled out**: a crash in code that runs during or right after the write (heap corruption or
  a stack overflow in the writer, e.g. the web server's task during a settings save, or a 2 KB copy
  on a small stack), a watchdog during a long LittleFS erase, and the supply dipping during a write
  (that would read as a brown-out, not a panic).
- **What the record adds**: the panic hook now notes whether a flash write was in progress
  (`flashWrite`, from `FlashGuard`'s lock-free mirror: settings and NVS saves, the fault record, the
  kept summaries, uploads), beside the reason (a cache error says "Cache disabled but cached memory
  region accessed"), the PC and backtrace, the task and its stack left. The next panic will say
  whether the hypothesis holds and, if so, which code ran.

## R12. A shared-heritage suspect: the live settings written while other tasks read them

SQMeter shows the same unexplained panics, so the code both devices share was read for a cause.
One pattern stands out and fits every report (settings saves, the start-up syncs, busy pages):

- **The settings are one global `Config`, replaced in place by one task while other tasks read it
  without a lock.**
  - RoofRoof: `src/main.cpp` `updateConfigCallback()` runs on the web server's task (`async_tcp`)
    and does `config = *next;` under `configSaveLock`; `src/SiteSync.cpp`
    `SiteSync::syncSettings()` and the location save write parts of `config` from the main loop
    (`Site::applyOutcome`, `config.location = *next`). The readers don't take that lock:
    `getConfigCallback()` hands out `const Config &` to every handler (`src/WebServer*.cpp`, about 36
    call sites, on `async_tcp`) and to the main loop (`WebServer::processAlerts`,
    `WebServer::publishDiscovery`, `handleSafety`, ...), which also reads `config.*` directly.
  - SQMeter: `src/main.cpp` `saveConfigCallback()` (`config = newConfig;`), called from
    `src/WebServerApi.cpp` (the settings save, the Wi-Fi join) and `src/WebServer.cpp` on `async_tcp`,
    while `loop()` and the main loop's publishers and alert checks read `config` through
    `getConfigCallback()` and directly.
- **Why it can panic**: assigning a `Config` reassigns dozens of `std::string`s (and the alert
  templates); a string that grows is reallocated and its old buffer freed. A reader on the other
  task (or core) holding a `const char *` from `c_str()`, copying it, or serialising it into JSON at
  that moment reads freed or half-written memory: a `LoadProhibited`/`StoreProhibited` panic, or heap
  corruption that panics later in `malloc`/`free` (`assert failed: ... heap`, `abort()`), often in a
  different task from the one that saved. A run of quick saves (the panic during a burst of small
  settings saves) widens the window; so do the start-up syncs from SQMeter (the panics a few seconds
  after boot) while the dashboard, the Alpaca client (N.I.N.A. polling the Dome) and MQTT read the
  settings.
- **Not proven**: the panics may still have another cause; the record of the next one will say
  (reason, task, backtrace into `std::string`/`Config::operator=`/`malloc`).
- **Not fixed here**: a fix changes how every reader gets the settings (an immutable snapshot
  swapped as a whole, e.g. `std::shared_ptr<const Config>` replaced atomically and held by each
  reader for its pass or request; or one reader-writer lock that every reader takes), across about
  40 call sites in both projects. That is a change to the platform's settings model, its own spec
  with its own tests, not a diagnostics change; it is proposed as the next spec and recorded as an
  owner assumption.

## R13. Field evidence: GET /api/config reproduces the panic

- **Report**: on the bench board (main f3c98f6) six clients looping `GET /api/status`, `/api/diag`,
  `/api/config` and the Dome's `shutterstatus` for 5 minutes gave about 10 panics (`ESP_RST_PANIC`)
  with the roof at rest and no flash write; minimum free heap 59-122 KB and every stack well clear.
  Each endpoint alone with 3 clients for 60 s: only `/api/config` restarted the board; status, diag
  and Alpaca together: none. Saves (`POST /api/config`) had panicked too.
- **Cause found in the code**: `WebServer::handleGetConfig` (`src/WebServerApi.cpp`) built the reply
  with `Config::toJson(true)`: an ArduinoJson pool of 9.7 KB, then `serializeJson` into a
  `std::string` that grows by doubling to a 16 KB capacity (copying 8 KB into 16 KB on the way),
  then `beginResponse(200, type, json.c_str())`, which copies the text into a ~10 KB Arduino `String`
  kept until the reply is sent. Each request therefore needs a contiguous 16 KB block and about
  45 KB at its peak, and holds 10 KB per reply still in flight. Several readers at once fragment the
  heap; once no 16 KB block is left, the `std::string` allocation fails. The build has no C++
  exceptions (`CONFIG_COMPILER_CXX_EXCEPTIONS=n`), so libstdc++'s `bad_alloc` is `abort()`: a panic on
  `async_tcp`, with plenty of total free heap. (The free-heap figure in `/api/status` is sampled
  between requests, so it doesn't show the peak.)
- **Fix**: `GET /api/config` fills the JSON document from `ConfigJson::write` (the body of
  `Config::toJson`, moved to `lib/ConfigModel/include/ConfigJson.h`) into a pool whose allocation is
  checked, writes the text into one checked buffer the size of the pool, shrinks it to the text and
  hands it to the response, which frees it when sent: no growing string, no second copy, and `503`
  instead of an abort when memory is short. `POST /api/config` is refused with `503` (nothing saved)
  while the largest free block is under 20 KB or the free heap under 48 KB, since a save builds
  several settings-sized strings and documents. `test/test_config_json_body` checks the document is
  the same as `toJson` and that the longest settings fit the pool and the buffer;
  `tools/load_config_get.py` is the board-level check (N clients reading the settings for 5 minutes;
  pass: no restart).
- **Not on the board yet**: this branch hasn't been flashed (bench check).
- **SQMeter**: the same handler in `src/WebServerApi.cpp` `WebServer::handleGetConfig`
  (`cfg.toJson(true)` into a `std::string`, then `request->send(200, "application/json",
  json.c_str())`) and the same save path (`handleSetConfig` → `saveConfigCallback`, `Config::toJson`
  in `lib/` and `src/ConfigStore.cpp`), built without C++ exceptions as well; the same fix applies.
- **Move end**: nothing at the end of a move saves the settings or bumps the settings revision. The
  revision moves only on a settings save (`updateConfigCallback`) and on SiteSync's saves
  (`siteSaved`: the location from SQMeter, the followed groups). The dashboard reads `/api/config`
  again on every revision change and on every (re)connection (`web/src/components/Dashboard.tsx`), so
  every open page reading at once after a restart or reconnection is the burst that matters.

