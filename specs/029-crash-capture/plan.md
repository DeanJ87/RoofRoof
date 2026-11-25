# Implementation Plan: Crash Capture

**Branch**: `spec/029-crash-capture` | **Date**: 2026-11-25 | **Spec**: [spec.md](spec.md)

## Summary

- `lib/CrashLogic` (new, pure): the check value, the RTC layouts (`Crash::Capture`,
  `Crash::Snapshot`, `Crash::LogRing` with its lines), the ring's start, push and copy, text
  clean-up, the record (`Crash::Record`, which keeps the capture and snapshot as read) built from the
  reset code, capture, snapshot and log lines, its blob form and version check, the slot rotation,
  the reset-reason tokens and kinds, and the two plain-text documents. Native tests first.
- Firmware: `src/CrashLog.cpp` (new) owns the RTC memory (capture, snapshot, ring), installs the
  Arduino core's panic hook and the task watchdog hook, takes the snapshot once a second from the
  main loop, and at start turns the reset into a record in NVS (namespace `crashlog`). `Logger`
  copies each line into the ring; `FlashGuard` gains a lock-free "writing now" the hook records.
  `src/WebServerCrash.cpp` (new) serves `GET /api/diag/crashes` and `GET /api/diag/log` (text).
  `platformio.ini` adds the linker's `--wrap=esp_panic_handler`; `scripts/keep_elf.py` (new) checks
  that the image carries its `.elf`'s SHA-256 and keeps the last 5 `.elf` files under `.pio/elf/`.
- `tools/decode_crash.py` (new) with its tests; `tools/firmware/test_isr_audit.py` (new); CI uploads
  the `.elf`, a release attaches it.
- Demo core: the emulated device serves the same documents (no records; one log line).
- Web: a Crashes card on the Diagnostics page (shown with diagnostics on or off), i18n, mocks,
  Vitest, Playwright.
- Route registry, OpenAPI (plain-text responses); docs (Diagnostics, REST, a developer page on
  decoding crashes), CHANGELOG, roadmap.
- Withdrawn for flash after spec 027 (research R9): the restart alert, the MQTT device message,
  the Home Assistant sensor, JSON documents, the ESP-IDF log hook.

Decisions are in [research.md](research.md); types in [data-model.md](data-model.md); the
interfaces in [contracts/crashes.md](contracts/crashes.md).

## Technical Context

| | |
|---|---|
| **Language/Version** | C++17 (GCC 14, Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5, pioarduino 55.03.312-1); TypeScript strict; Python 3.10+ |
| **Primary Dependencies** | Arduino core `set_arduino_panic_handler` (esp32-hal-misc.c); ESP-IDF `esp_task_wdt_isr_user_handler`, `esp_task_wdt_print_triggered_tasks`, `esp_app_get_elf_sha256`, `esp_reset_reason`; Preferences (NVS); esptool 5.4 `--elf-sha256-offset` (passed by pioarduino's hybrid compile); `xtensa-esp32-elf-addr2line` |
| **Storage** | RTC slow memory (no init): about 4.7 KB of the 7.6 KB free. NVS: namespace `crashlog`, 3 blobs of about 860 bytes and one counter. No LittleFS change |
| **Testing** | `pio test -e native` (new `test_crash_ring`, `test_crash_record`, `test_crash_documents`, `test_hall_flap`); `python3 -m pytest tools/test_decode_crash.py`; Vitest; Playwright |
| **Target Platform** | ESP32 DevKit (WROOM, 4 MB) now, WT32-ETH01 later: same image |
| **Project Type** | Embedded firmware + web UI + tools + docs |
| **Performance Goals** | a log line costs one `vsnprintf` into 72 bytes and a copy under a spinlock; the snapshot runs once a second on the main loop |
| **Constraints** | hooks copy only (no allocation, lock, flash, log); the one flash write at start, before the control task; flash growth < 12 KB, image ≤ 95% of the slot (baseline 1,703,984 B, 92.9%); UI growth < 3 KB gz; Config.h stays ≤ 600 lines |
| **Scale/Scope** | 50 log lines, 3 records, 16 frames, 8 log lines per record |

## Constitution Check

| Principle | How this plan satisfies it | Tests |
|---|---|---|
| I. Motion Safety | The hooks run only on the way to a restart and only copy. The log copy takes a spinlock for one 84-byte copy; the roof's task logs rarely (faults, ends) and the copy is far below its 20 ms step. The snapshot is on the main loop. The NVS write is in `setup()` after `PinSetup::begin` (EN low) and before `roofControl.begin` (no motion possible). Nothing feeds the controller. | `test_crash_ring` (bounded push); review of `setup()` order |
| II. Parked mount | Untouched. | - |
| III. Unsafe ends closed | Untouched (FR-024). | - |
| IV. Alpaca | Untouched. | ConformU CI |
| V. Pure logic | Layouts, checks, ring, record, rotation, tokens and documents in `lib/CrashLogic` with native tests; decode tool with Python tests. | native, pytest |
| VI. Budgets | RTC memory, not heap, for the ring, capture and snapshot; handlers copy (ring under its spinlock, records from NVS) then send in chunks; flash and UI growth measured; stack and heap measured on the bench (pending). | build, `flash_budget.py`, `size_check.py` |
| VII. UI | One card on the Diagnostics page built from `Card`, `Pill`, `ReadingRow`, `Button`, `Note`; i18n; a11y; phone width. | Vitest, Playwright + axe, `copy_check.py`, `label_check.py` |
| VIII. Security | Both reads call `requireAuth` (log lines can name hosts and topics). No secret is logged (LOG-02). | `tools/api/routes.py`, review |
| IX. Docs | Diagnostics, REST, decoding crashes; OpenAPI regenerated. | `mkdocs build --strict`, `openapi.py --check` |
| X. Quality | check.py, function limits. | check.py |
| XI. Heritage | SQMeter spec 038's `<base>/device` names reused; no SQMeter module changed. | - |

**Gate**: pass, no exceptions.

## Project Structure

```text
specs/029-crash-capture/ spec, plan, research, data-model, quickstart, tasks, contracts/, checklists/
lib/CrashLogic/ (new) include/{CrashTypes,LogRing,CrashRecord,CrashDocuments}.h,
  src/{LogRing,CrashRecord,CrashDocuments}.cpp, library.json
include/CrashLog.h, src/CrashLog.cpp, src/WebServerCrash.cpp (new); src/Logger.cpp, src/main.cpp,
  include/WebServer.h, src/WebServerApi.cpp, include/FlashGuard.h, src/FlashGuard.cpp
platformio.ini, scripts/keep_elf.py (new), .github/workflows/build.yml
tools/decode_crash.py, tools/test_decode_crash.py, tools/firmware/test_isr_audit.py (new)
tools/demo-core/ emulated_device*.{h,cpp}, bridge.cpp, build.sh
test/test_crash_ring, test/test_crash_record, test/test_crash_documents, test/test_hall_flap (new)
web/src: components/diagnostics/CrashesCard.tsx, lib/crashApi.ts, types/crash.ts, mocks/crash.ts (new);
  components/Diagnostics.tsx, index.css, demo + mocks handlers, i18n
web/tests/diagnostics.spec.ts
tools/api/{routes,operations}.json, tools/api/openapi.py (plain-text responses), docs/api/openapi.json
docs/user-guide/diagnostics.md, docs/api/rest.md, docs/development/crash-decoding.md (new),
  mkdocs.yml, CHANGELOG.md, specs/ROADMAP.md
```

**Structure Decision**: one pure library for everything the feature decides and formats, one
firmware module that owns the RTC memory, the hooks and the NVS records, and the routes in their own
WebServer file.

## Phases

1. **Pure logic, tests first**: check value, ring, record, rotation, tokens, documents.
2. **Firmware glue**: `CrashLog`, Logger, `setup()` order, routes; linker wrap and ELF SHA; zero
   warnings; flash measured.
3. **Tools and CI**: `decode_crash.py` with tests, the interrupt audit, ELF artifacts.
4. **Demo core and web**: documents in the emulated device; Crashes card; Vitest, Playwright; OpenAPI.
5. **Docs**.
6. **Verify**: quickstart §1 of spec 001, converge.

## Complexity Tracking

None.
