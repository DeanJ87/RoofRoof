#pragma once

// Crash capture (spec 029, data-model.md): the layouts kept in RTC memory
// across a restart (the panic's capture, the main loop's snapshot) and their
// check values. Fixed-width fields only: the firmware writes them from the
// panic hook, where nothing may allocate, lock or log.

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace Crash
    {
        // ESP-IDF's esp_reset_reason_t numbers (src/CrashLog.cpp checks each one).
        enum class ResetCode : uint8_t
        {
            Unknown = 0,
            PowerOn = 1,
            External = 2,
            Software = 3,
            Panic = 4,
            InterruptWatchdog = 5,
            TaskWatchdog = 6,
            OtherWatchdog = 7,
            DeepSleep = 8,
            Brownout = 9,
            Sdio = 10,
            Usb = 11,
            Jtag = 12,
            Efuse = 13,
            PowerGlitch = 14,
            CpuLockup = 15,
        };

        // What kind of unexpected restart a reset was; None makes no record.
        enum class Kind : uint8_t
        {
            None = 0,
            Panic = 1,
            Watchdog = 2,
            Brownout = 3,
        };

        constexpr size_t MAX_FRAMES = 16;
        constexpr size_t REASON_CHARS = 48;
        constexpr size_t TASK_CHARS = 16;
        constexpr size_t WATCHDOG_TASKS_CHARS = 32;
        constexpr size_t ROOF_CHARS = 12;
        // A stack whose headroom couldn't be read.
        constexpr uint16_t STACK_UNKNOWN = 0xFFFF;

        // FNV-1a over `length` bytes: the check value of every kept layout.
        uint32_t check(const void *bytes, size_t length);

        // Copies `text` into `out` (always terminated, cut to fit); null copies "".
        void copyText(char *out, size_t size, const char *text);

        // What the panic hook writes (FR-001, FR-002).
        struct Capture
        {
            static constexpr uint32_t MAGIC = 0xC2A5C001u;
            static constexpr uint8_t CORRUPT = 1u << 0;     // the backtrace hit a bad frame
            static constexpr uint8_t CONTINUES = 1u << 1;   // more frames than MAX_FRAMES
            static constexpr uint8_t FLASH_WRITE = 1u << 2; // a flash write was in progress (src/FlashGuard)
            uint32_t magic;
            uint32_t uptimeMs;
            uint32_t pc;
            uint32_t backtrace[MAX_FRAMES];
            uint8_t frames;
            uint8_t core;
            uint8_t flags;
            uint8_t pad;
            char reason[REASON_CHARS];
            char task[TASK_CHARS];
            char watchdogTasks[WATCHDOG_TASKS_CHARS];
            // The crashing task's own stack left (bytes), read last: a stack
            // overflow shows here even when the snapshot is a second old.
            uint16_t taskStackFree;
            uint16_t pad2;
            uint32_t check;
        };

        // What the main loop writes about once a second (FR-003).
        struct Snapshot
        {
            static constexpr uint32_t MAGIC = 0xC2A5C002u;
            uint32_t magic;
            uint32_t uptimeMs;
            uint32_t epoch; // 0: the clock wasn't set
            uint32_t freeHeap;
            uint32_t minFreeHeap;
            uint32_t largestBlock;
            uint16_t stackLoop;
            uint16_t stackRoof;
            uint16_t stackNetwork;
            uint16_t stackPoll;   // the weather and park poller (outgoing HTTP/TLS)
            uint16_t stackAlerts; // the alert sender (TLS)
            uint16_t pad;
            char roof[ROOF_CHARS];
            uint32_t check;
        };

        // Marks a layout whole: its magic, then the check over every byte before `check`.
        template <typename T> void seal(T &kept)
        {
            kept.magic = T::MAGIC;
            kept.check = check(&kept, offsetof(T, check));
        }

        // The layout was sealed and nothing changed since (random memory after a power-on fails).
        template <typename T> bool intact(const T &kept)
        {
            return kept.magic == T::MAGIC && kept.check == check(&kept, offsetof(T, check));
        }
    } // namespace Crash
} // namespace SQM
