# Feature Specification: Crash Capture

**Feature Branch**: `spec/029-crash-capture`

**Created**: 2026-11-25

**Status**: Draft

## Context

The controller restarts on its own now and then. The status document says why the last start
happened (`resetReason`, for example 4 for a crash), but nothing more: which part of the firmware
failed, which task was running, how much memory and stack were left, and what the device was doing
just before. On the bench a USB cable and a serial monitor show the panic message and its
backtrace; the installed controller sits far from any cable, so that evidence is lost at every
crash. One crash happened during a quick run of small settings saves; it could not be followed up.

ESP-IDF can keep a full core dump in a flash partition, but the partition layout fills the 4 MB
chip (two 1.75 MB app slots, 448 KB LittleFS, NVS) and the table can only change over USB. This
spec takes another route that fits an over-the-air update:

- when the firmware panics (an exception, `abort()`, a failed assertion, a stack overflow, an
  interrupt or task watchdog), it writes a short **crash capture** into memory that survives the
  restart: the reason, the program counter and the backtrace addresses, the task and core, and
  for a task watchdog the tasks that stopped;
- once a second the main loop writes a **context snapshot** into the same memory: uptime, time,
  free heap, smallest free heap, largest block, stack headroom of the main loop, roof and network
  tasks, and the roof's state. A brown-out or another watchdog has no hook, so its record is built
  from the last snapshot;
- the device's own log lines also go into a **log ring** of the last 50 lines in that memory, so
  the lines before a crash are still there after it;
- at the next start, a restart caused by a crash, a watchdog or a brown-out becomes a **crash
  record** in NVS (flash): the last 3 are kept, each with the firmware version and a build
  identifier, the capture, the snapshot and the last 8 log lines;
- the records and the log ring are read-only over REST as plain text, and shown in a Crashes card
  on the Diagnostics page with Copy buttons;
- `tools/decode_crash.py` turns a record's addresses into function names, files and lines with the
  firmware's `.elf` from the same build, and checks that the `.elf` matches the record. Each build
  carries its `.elf`'s SHA-256 in the image, CI keeps the `.elf` with every build, and a release
  attaches it.

Crash capture observes; it never decides. Nothing here feeds the motion logic, the safety
verdict, the close sequence or the gates, and nothing here can move or stop the roof.

## Clarifications

### Session 2026-11-25

Each answer takes the safest option the constitution allows and is also listed under
"Assumptions decided for the owner".

- Q: A flash core dump partition, or memory that survives a restart? → A: Memory that survives a
  restart (RTC slow memory, not cleared at a software reset or a crash). The layout fills the
  chip; changing it needs a USB flash of every device (constitution VI, spec 027 of SQMeter), and
  a core dump writes about 64 KB of flash from the panic handler. A capture of a few hundred bytes
  in RTC memory is written without flash and copied to NVS at the next start.
- Q: How is the panic caught? → A: Through the Arduino core's own panic hook
  (`set_arduino_panic_handler`), which the core attaches by wrapping ESP-IDF's
  `esp_panic_handler` at link time; the build adds the linker's wrap flag that PlatformIO leaves
  out. The hook only copies into RTC memory: no allocation, no lock, no flash, no log call. The
  task watchdog's interrupt hook (`esp_task_wdt_isr_user_handler`) adds the names of the tasks
  that stopped, before the watchdog's own panic.
- Q: What does a brown-out or a watchdog without a panic record? → A: The reset reason and the last
  context snapshot (at most a second old), marked "no capture". RTC memory may be garbage after a
  brown-out: every part (capture, snapshot, each log line) carries a check value and is used only
  when it is intact.
- Q: How many records, and where? → A: The last 3, in NVS under their own namespace, about 800
  bytes each (about 90 of the 342 free NVS entries on the bench device). Written once at start,
  before the roof's control task starts, so no write waits behind a move (spec 010). A power-on,
  a reset by the button or a software restart (an update, a settings restart) makes no record.
- Q: JSON or text? → A: Plain text, one fact per line. The image sits near its slot's limit; a
  hand-written text record costs a fraction of a JSON writer, and a person, a copy-paste into an
  issue and the decoding tool all read text.
- Q: An alert, MQTT and a Home Assistant sensor for the restart? → A: Not in this spec. They cost
  about 5 KB of flash the image can't spare beside specs 027 and 030; the records are on the device
  and in the web UI, and the alert and sensor can follow once there is room (Out of scope).
- Q: Who may read the records and the log? → A: Only with the password when protection is on
  (`requireAuth`): log lines can name addresses, hosts and topics, which is more than readings
  (constitution VIII). Nothing in them is a secret (LOG-02 already keeps secrets out of the log).
- Q: Can the records be cleared? → A: No; they are read-only and the oldest is replaced by the
  next. A clear command is a state change that needs its own guard and gains little with 3 records.
- Q: Which log lines go into the ring? → A: Every line the device's logger prints (info and above
  in a release build). ESP-IDF's and the Arduino core's own lines go to the serial port only (a hook
  for them cost flash the image can't spare). Each line keeps its time since boot, its level and up to 71 characters of
  text; the ring marks which boot a line belongs to.
- Q: How does a person match a record to the right `.elf`? → A: Each image carries the SHA-256 of
  its own `.elf` (esptool's `--elf-sha256-offset`, which this build already passes; a build step
  now checks it); the record keeps its
  first 16 hex digits; `decode_crash.py` refuses an `.elf` whose SHA-256 doesn't start with them
  unless told `--force`. CI uploads the `.elf` with each build; a release attaches it gzipped; a
  local build keeps the last 5 under `.pio/elf/`.

## Constitution rules this spec relies on

| Principle | Rule | Where it is decided |
|---|---|---|
| I | Motion runs in its own task; nothing else may delay it; EN is driven low first at boot | FR-004, FR-009: the hook only copies; the snapshot runs on the main loop; the record is written after EN is low and before the control task starts |
| I | Flash writes wait while the roof moves (spec 010) | FR-009: the only flash write is at start, before the roof can move |
| III | Alerts and diagnostics report; they never change the verdict or the motion | FR-024: nothing here feeds the motion logic, the gates, the close sequence or the verdict |
| VI | Image within the OTA slot; heap and stack measured; handlers don't block | FR-026, FR-027: RTC memory, not heap; replies built from a copy; growth measured |
| VII | Quiet UI | FR-019: one card on the Diagnostics page, not on the dashboard |
| VIII | Endpoints that reveal more than readings call `requireAuth` | FR-014 |
| IX | Docs move with behaviour | FR-028 |

## User Scenarios & Testing *(mandatory)*

### User Story 1 - See why the controller crashed (Priority: P1)

The owner finds the controller's uptime short in the morning. They open the Diagnostics page;
the Crashes card lists the crash: when it happened, "Crash (panic)", "LoadProhibited", the task
`async_tcp`, the firmware version, the free memory and stack headroom just before, and the last
log lines ("Settings saved" five times). They press Copy and paste the record into an issue.

**Why this priority**: Without the record, a crash at the roof leaves no evidence at all.

**Independent Test**: On a bench board, build with the rollback test flag that calls `abort()`
after start; after the restart `GET /api/diag/crashes` lists one record whose reason names
`abort()`, whose task is `loopTask` and whose backtrace has at least one address.

**Acceptance Scenarios**:

1. **Given** the firmware panics, **When** the device starts again, **Then** a crash record exists
   with the reset reason, the panic reason, the program counter, up to 16 backtrace addresses
   (marked corrupt or cut short when they are), the task and core, the firmware version and build
   identifier, the uptime and (with a set clock) the time of the crash, the heap and stack figures
   from the last snapshot and the last 8 log lines.
2. **Given** three records exist, **When** a fourth crash happens, **Then** the oldest is replaced
   and the other two are kept.
3. **Given** a power cut, **When** the device starts, **Then** the records from before are still
   listed (they are in NVS) and no new record is made.
4. **Given** a software restart (a settings restart, an update), **When** the device starts,
   **Then** no record is made.

---

### User Story 2 - Turn the addresses into code (Priority: P1)

The maintainer saves the copied record as `crash.json`, downloads the `.elf` of that build from
the CI run or the release, and runs `python3 tools/decode_crash.py crash.json --elf firmware.elf`.
The tool checks the build identifier and prints each frame as an address, a function, a file and
a line.

**Why this priority**: Addresses alone don't say what failed.

**Independent Test**: Feed the tool a record whose addresses are functions in a known `.elf`; it
prints their names; feed it a different `.elf` and it refuses.

**Acceptance Scenarios**:

1. **Given** a record and the matching `.elf`, **When** the tool runs, **Then** it prints the PC and
   each backtrace frame with the function, file and line.
2. **Given** an `.elf` whose SHA-256 doesn't match the record's build identifier, **When** the tool
   runs, **Then** it stops with a message naming both, unless `--force` is given.
3. **Given** only addresses (`--addrs 0x400d1234:0x3ffb1230 …` as the serial monitor prints them),
   **When** the tool runs with an `.elf`, **Then** it decodes them the same way.

---

### User Story 3 - Read the recent log (Priority: P2)

Something odd happened but the device didn't crash. The owner presses Copy log on the Crashes
card and gets the last 50 log lines, including the lines before the last restart.

**Why this priority**: The log is useful beyond crashes and there is no other way to read it
without a cable.

**Independent Test**: `GET /api/diag/log` returns up to 50 lines of text, oldest first, each with its
boot, time since boot, level and text.

**Acceptance Scenarios**:

1. **Given** the device logged more than 50 lines, **When** the log is read, **Then** the 50 newest
   are returned, oldest first.
2. **Given** a crash, **When** the log is read after the restart, **Then** the lines from before the
   crash are still there, marked with the earlier boot.
3. **Given** a power-on, **When** the log is read, **Then** only this boot's lines are returned.

### Edge Cases

- A crash inside the panic hook itself, or a second panic: ESP-IDF's own handler still runs and
  restarts the device; the capture is either whole (its check value matches) or ignored.
- A stack overflow: the capture still records the reason ("Stack canary watchpoint triggered") and
  the task; its backtrace may be marked corrupt.
- The backtrace is longer than 16 frames: the first 16 are kept and the record says it continues.
- A panic with no capture (a crash in the earliest start-up, before the hook is set): the record
  says "no capture" and keeps the reset reason and any snapshot.
- A watchdog reset other than the task or interrupt watchdog (the RTC watchdog): no hook; the
  record comes from the snapshot.
- RTC memory garbage after a brown-out or power-on: the capture, snapshot and lines whose checks
  fail are dropped; the ring starts empty.
- NVS full or failing at start: the record isn't kept; the failure is logged (and so in the ring);
  the device starts normally.
- An older firmware's records (another record version): skipped when read.
- The clock isn't set at the time of the crash: the time says unknown; the uptime is still there.
- A capture from an earlier crash that was already stored: it is cleared once stored, so it can't
  be stored twice.
- A log line longer than 71 characters: cut, never wrapped. Control characters and colour codes are
  removed.
- Many tasks logging at once: each line is written whole under a short lock; none is torn.

## Requirements *(mandatory)*

### Functional Requirements

Capture

- **FR-001**: On a panic (an exception, `abort()`, an assertion, a stack overflow, the interrupt or
  task watchdog) the firmware MUST write a crash capture into RTC memory that survives the restart:
  the panic reason (up to 47 characters), the core, the program counter, up to 16 backtrace
  addresses with "corrupt" and "continues" flags, the running task's name on that core, that
  task's stack left (read last, so a fault while reading it can't lose the rest), the uptime in
  milliseconds and a check value.
- **FR-002**: On a task watchdog timeout the firmware MUST add the names of the tasks that didn't
  reset the watchdog (up to 31 characters) to the capture.
- **FR-003**: The main loop MUST write a context snapshot into RTC memory about once a second: the
  uptime, the clock time (or none), free heap, smallest free heap since boot, largest free block,
  the stack headroom of the main loop, the roof's control task, the network task, the weather and
  park poller and the alert sender, the roof's state, and a check value.
- **FR-004**: The panic and watchdog hooks MUST only copy into RTC memory: no allocation, no lock,
  no flash access, no logging, and no change to how ESP-IDF then reports and restarts.
- **FR-005**: Every line the device's logger prints MUST also go into
  a ring of the last 50 lines in RTC memory, each with the boot it belongs to, its time since boot,
  its level, up to 71 characters of text without control characters, and a check value. A line is
  written whole under a short lock.
- **FR-006**: At start the ring MUST be kept when its header and lines check out (a software
  restart or a crash), and emptied otherwise; lines whose check fails MUST be dropped.

Records

- **FR-007**: At start, when the reset reason is a panic, a watchdog (interrupt, task or other) or a
  brown-out, the firmware MUST build a crash record from the reset reason, the capture (when intact)
  and the snapshot (when intact), with the firmware version, the first 16 hex digits of the image's
  `.elf` SHA-256, the time of the crash estimated from the snapshot's clock (or null), and the last
  8 log lines from before this start.
- **FR-008**: Records MUST be kept in NVS, the last 3, each numbered by a sequence that keeps rising
  across restarts; the oldest is replaced by the newest. Records of another format version are
  skipped when read.
- **FR-009**: The record MUST be written once per start, after the motor driver's EN is driven low
  and before the roof's control task starts; the capture is cleared once stored.
- **FR-010**: A power-on, a reset by the EN button, a software restart and a deep-sleep wake MUST NOT
  make a record.
- **FR-011**: A failure to read or write the records MUST be logged and MUST NOT stop the start.

Interfaces

- **FR-012**: `GET /api/diag/crashes` MUST return this start's reset reason and the records (newest
  first) as plain text, one fact per line, as documented in `contracts/crashes.md`, with each record's
  PC and frames on one `Backtrace:` line the decoding tool reads.
- **FR-013**: `GET /api/diag/log` MUST return the ring's lines as plain text, oldest first, each with
  its boot.
- **FR-014**: Both endpoints MUST call `requireAuth` and change nothing; both are built from a copy
  taken under the ring's lock or from NVS, never while holding the lock.
- **FR-015 to FR-018**: (withdrawn for flash, see Out of scope: the MQTT device message, the Home
  Assistant sensor and the restart alert.)

Web UI

- **FR-019**: The Diagnostics page MUST show a Crashes card whether advanced diagnostics is on or
  off: each record's time (or uptime), kind, reason, task, firmware version, heap and stack figures
  and backtrace (the device's text for each record), a Copy button that copies the record (what the
  decoding tool reads), and a Copy log button that copies the log ring. With no records, the card says
  so.
- **FR-020**: (withdrawn with FR-017.)

Tools and builds

- **FR-021**: `tools/decode_crash.py` MUST decode a record (text from the endpoint or the Copy
  button, a file or stdin) or a list of addresses with an `.elf` (plain or gzipped) through the
  Xtensa `addr2line`, printing the PC and each frame with function, file and line, and MUST refuse
  an `.elf` whose SHA-256 doesn't match the record's build identifier unless `--force`.
- **FR-022**: Every `esp32dev` build MUST carry its `.elf`'s SHA-256 in the image's app
  description, checked after the build (the build fails otherwise); a local build MUST keep the last 5 `.elf` files gzipped under `.pio/elf/` named by
  that identifier.
- **FR-023**: CI MUST upload the `.elf` with each build, and a release MUST attach it.

Boundaries

- **FR-024**: Nothing in this spec MUST feed the motion logic, the gates, the close sequence, the
  safety verdict or the platform guard.
- **FR-025**: The record format, the ring, the check values, the reset-reason names and the text
  documents MUST live in `lib/` with native tests; `src/` only reads hardware state and NVS.
- **FR-026**: The image MUST stay within 95% of its OTA slot; the growth is measured and recorded.
- **FR-027**: The ring, capture and snapshot MUST use RTC memory, not heap; the heap and stack
  headroom after the change MUST be measured on a device (bench check).
- **FR-028**: The user guide (Diagnostics), the REST reference,
  the OpenAPI document, the contract schemas and a developer page on decoding crashes MUST be
  updated.

### Key Entities

- **Crash capture**: what the panic hook writes (reason, PC, backtrace, task, core, watchdog tasks,
  uptime, check); in RTC memory, cleared once stored.
- **Context snapshot**: what the main loop writes each second (uptime, clock, heap, stacks, roof
  state, check); in RTC memory.
- **Log ring**: the last 50 log lines with boot, time, level and text; in RTC memory.
- **Crash record**: one unexpected restart (sequence, kind, reset code, firmware, build id, the
  capture's and snapshot's fields, 8 log lines); the last 3 in NVS.

## Design *(mandatory for UI changes)*

- **DS rules**: DS-01 (a `Card` on the Diagnostics page, with a "?" hint), DS-02 (the kind as a
  `Pill`: red for a crash or watchdog, amber for a brown-out), DS-03 (`ReadingRow`s for the
  figures), DS-04 (the explanation behind "?"), DS-05 (the backtrace monospace and left-to-right),
  DS-07 (Copied is a toast), DS-20 to DS-25 (copy limits).
- **Mockup**: not reviewed by the owner; the card reuses the existing Diagnostics parts only (see
  Assumptions).
- New English strings: card title "Crashes" (label), hint (hint), "No crashes recorded" (note),
  "Copy", "Copy log" (labels), kind pills "Crash", "Watchdog", "Brown-out" (pills), row labels
  "Reason", "Task", "Firmware", "Free memory", "Stack left", "Backtrace", "Recent log" (labels),
  "Copied" (toast).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After a crash at the roof, the reason, the task and at least the PC of the crash can
  be read from the web UI without a cable, in every case where the panic handler ran.
- **SC-002**: A copied record decodes to function names and lines with one command and the build's
  `.elf`.
- **SC-003**: (withdrawn with the alert and the sensor.)
- **SC-004**: The firmware image grows by less than 8 KB and stays below 95% of the OTA slot; the
  device UI's gzipped size grows by less than 3 KB.
- **SC-005**: The motion task's timing and the main loop's and roof task's stack headroom stay
  above 2 KB (constitution VI), measured on the bench.

## Assumptions decided for the owner

- RTC memory instead of a core dump partition: the layout stays as it is, so the change ships over
  the air. A full core dump (every task's stack) is not available; the record holds the crashing
  task's backtrace only.
- 3 records of about 800 bytes in NVS; 50 log lines of 71 characters and 8 lines per record.
  NVS had 342 free entries on the bench device; the records take about 90.
- The records and the log need the password when protection is on (constitution VIII); there is no
  clear command; the oldest record is replaced.
- No restart alert, MQTT message or Home Assistant sensor in this spec (flash); a follow-up adds them
  once there is room. The records and the Crashes card are how a restart is found meanwhile.
- A brown-out makes a record too, although the ESP32 also reports a slow power-up as a brown-out; the
  docs say so.
- Only the device's own logger fills the ring; ESP-IDF's and the Arduino core's lines go to the
  serial port only (a hook for them cost flash).
- The documents are plain text, not JSON (flash); one fact per line, the backtrace on one line.
- The Crashes card shows on the Diagnostics page with diagnostics off as well, since crash capture
  is always on; it isn't on the dashboard (constitution VII). The mockup review is pending: the card
  uses only the page's existing parts.
- The demo shows an empty Crashes card and its own log lines; there is no crash injector.
- The crash during the run of small settings saves, and the panics while the Hall input flapped
  (research R10), are not fixed here: the pure fault-store path was hammered natively and holds, and
  the record now names the crashing task and its stack, which is what will explain them.
- A likely shared cause was found but not fixed here (research R12): the live settings are replaced
  in place on one task while other tasks read them without a lock (RoofRoof `updateConfigCallback`,
  `SiteSync`; SQMeter `saveConfigCallback`). The fix changes the platform's settings model across
  both projects and is proposed as its own spec; this spec's records will confirm or rule it out.
- Flash: a first version with JSON documents, the alert, MQTT and Home Assistant grew the image by
  about 18 KB; after spec 027 took the image to 94.2% of the slot, this spec was cut back to the
  capture, the records, two text documents and the card: about 7.5 KB (research R9).

## Out of scope

- A core dump partition or any partition change.
- Uploading records anywhere automatically; a clear command; decoding on the device.
- Changing the watchdog timeouts or what the firmware does after a panic.
- A "Restarted unexpectedly" alert, a retained `<base>/device` MQTT message and a Home Assistant
  "Last restart reason" sensor (withdrawn for flash; a follow-up spec once there is room).
- JSON documents; ESP-IDF's and the Arduino core's log lines in the ring.
- Fixing the settings race found in research R12 (its own spec, in RoofRoof and SQMeter).
