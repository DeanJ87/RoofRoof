#include "CrashLog.h"

#include <Arduino.h>
#include <nvs.h>
#include <esp32-hal.h>
#include <esp_app_desc.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <utility>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

#include "DeviceCore.h"
#include "FlashGuard.h"
#include "Logger.h"
#include "version.h"

extern uint32_t bootCount; // src/main.cpp

// Spec 029 research R1-R6. The hooks run on the way to a restart: they only
// copy into RTC memory (no allocation, lock, flash or log), and ESP-IDF then
// prints and restarts exactly as before.

namespace SQM
{
    namespace CrashLog
    {
        using Crash::ResetCode;

        static_assert(static_cast<int>(ResetCode::PowerOn) == ESP_RST_POWERON, "reset codes");
        static_assert(static_cast<int>(ResetCode::Software) == ESP_RST_SW, "reset codes");
        static_assert(static_cast<int>(ResetCode::Panic) == ESP_RST_PANIC, "reset codes");
        static_assert(static_cast<int>(ResetCode::InterruptWatchdog) == ESP_RST_INT_WDT, "reset codes");
        static_assert(static_cast<int>(ResetCode::TaskWatchdog) == ESP_RST_TASK_WDT, "reset codes");
        static_assert(static_cast<int>(ResetCode::OtherWatchdog) == ESP_RST_WDT, "reset codes");
        static_assert(static_cast<int>(ResetCode::DeepSleep) == ESP_RST_DEEPSLEEP, "reset codes");
        static_assert(static_cast<int>(ResetCode::Brownout) == ESP_RST_BROWNOUT, "reset codes");
        static_assert(static_cast<int>(ResetCode::CpuLockup) == ESP_RST_CPU_LOCKUP, "reset codes");

        namespace
        {
            constexpr const char *TAG = "Crash";
            constexpr const char *NVS_NAMESPACE = "crashlog";
            constexpr uint32_t SNAPSHOT_EVERY_MS = 1000;

            RTC_NOINIT_ATTR Crash::Capture capture;
            RTC_NOINIT_ATTR Crash::Snapshot snap;
            RTC_NOINIT_ATTR Crash::LogRing ring;

            portMUX_TYPE ringLock = portMUX_INITIALIZER_UNLOCKED;
            bool ringReady = false;

            // The task watchdog's stopped tasks, named by its interrupt just
            // before the panic it raises.
            char watchdogTasks[Crash::WATCHDOG_TASKS_CHARS];
            size_t watchdogTasksLength = 0;

            // What arm() set aside from the last run, until keepRecord() stores it.
            struct Pending
            {
                ResetCode reset = ResetCode::Unknown;
                bool hasCapture = false;
                bool hasSnapshot = false;
                Crash::Capture capture{};
                Crash::Snapshot snapshot{};
                // The ring's lines at arm(): all from earlier runs (nothing logged yet).
                Crash::LogLine lines[Crash::RING_LINES];
                size_t lineCount = 0;
            };
            std::unique_ptr<Pending> pending;

            ResetCode bootReset = ResetCode::Unknown;
            char build[Crash::BUILD_CHARS] = "";

            void push(uint32_t ms, Crash::Level level, const char *text)
            {
                if (!ringReady)
                    return;
                portENTER_CRITICAL_SAFE(&ringLock);
                Crash::push(ring, ms, level, text);
                portEXIT_CRITICAL_SAFE(&ringLock);
            }

            // The ring as it is now, copied under its lock; read outside it.
            std::unique_ptr<Crash::LogRing> ringCopy()
            {
                std::unique_ptr<Crash::LogRing> copy(new (std::nothrow) Crash::LogRing()); // spec 031: null when short
                if (!copy)
                    return copy;
                portENTER_CRITICAL(&ringLock);
                memcpy(copy.get(), &ring, sizeof(ring));
                portEXIT_CRITICAL(&ringLock);
                return copy;
            }

            uint16_t stackLeft(TaskHandle_t task)
            {
                return task != nullptr ? static_cast<uint16_t>(uxTaskGetStackHighWaterMark(task)) : Crash::STACK_UNKNOWN;
            }

            uint32_t uptimeMs()
            {
                return static_cast<uint32_t>(esp_timer_get_time() / 1000);
            }

            void onPanic(arduino_panic_info_t *info, void *)
            {
                capture.magic = 0;
                capture.uptimeMs = uptimeMs();
                capture.pc = reinterpret_cast<uint32_t>(info->pc);
                const unsigned frames = info->backtrace_len < Crash::MAX_FRAMES ? info->backtrace_len : Crash::MAX_FRAMES;
                for (unsigned i = 0; i < frames; ++i)
                    capture.backtrace[i] = info->backtrace[i];
                capture.frames = static_cast<uint8_t>(frames);
                capture.core = static_cast<uint8_t>(info->core);
                capture.flags = (info->backtrace_corrupt ? Crash::Capture::CORRUPT : 0) |
                                (info->backtrace_continues || info->backtrace_len > Crash::MAX_FRAMES ? Crash::Capture::CONTINUES : 0) |
                                (FlashGuard::writingNow() ? Crash::Capture::FLASH_WRITE : 0);
                capture.pad = 0;
                Crash::copyText(capture.reason, sizeof(capture.reason), info->reason);
                Crash::copyText(capture.task, sizeof(capture.task), pcTaskGetName(xTaskGetCurrentTaskHandleForCore(info->core)));
                Crash::copyText(capture.watchdogTasks, sizeof(capture.watchdogTasks), watchdogTasksLength > 0 ? watchdogTasks : "");
                capture.taskStackFree = Crash::STACK_UNKNOWN;
                capture.pad2 = 0;
                Crash::seal(capture);
                // Last, after the seal: reading a stack that overflowed could
                // fault again, and the capture above must survive that.
                const TaskHandle_t crashed = xTaskGetCurrentTaskHandleForCore(info->core);
                if (crashed != nullptr)
                {
                    const UBaseType_t left = uxTaskGetStackHighWaterMark(crashed);
                    capture.taskStackFree = left < Crash::STACK_UNKNOWN ? static_cast<uint16_t>(left) : Crash::STACK_UNKNOWN - 1;
                    Crash::seal(capture);
                }
            }

            // esp_task_wdt_print_triggered_tasks' lines: a caption, then
            // "\n - ", the name and " (CPU n)" for each task.
            void appendWatchdogTask(void *, const char *text)
            {
                if (text == nullptr || strncmp(text, "Task watchdog", 13) == 0)
                    return;
                const bool separator = strcmp(text, "\n - ") == 0;
                if (separator && watchdogTasksLength == 0)
                    return;
                const char *part = separator ? ", " : text;
                for (size_t i = 0; part[i] != '\0' && watchdogTasksLength + 1 < sizeof(watchdogTasks); ++i)
                    watchdogTasks[watchdogTasksLength++] = part[i];
                watchdogTasks[watchdogTasksLength] = '\0';
            }

            void readBuild()
            {
                esp_app_get_elf_sha256(build, sizeof(build));
            }

            // Records `p`'s restart under the next sequence; false when NVS refused.
            bool store(nvs_handle_t nvs, const Pending &p, uint32_t seq)
            {
                Crash::RecordInputs in;
                in.reset = p.reset;
                in.capture = p.hasCapture ? &p.capture : nullptr;
                in.snapshot = p.hasSnapshot ? &p.snapshot : nullptr;
                in.firmware = FIRMWARE_VERSION;
                in.build = build;
                in.seq = seq;
                in.log = p.lines;
                in.logCount = p.lineCount; // build() keeps the last RECORD_LINES
                std::unique_ptr<Crash::Record> record(new Crash::Record());
                if (!Crash::build(in, *record))
                    return false;
                char key[4];
                snprintf(key, sizeof(key), "r%u", static_cast<unsigned>(Crash::slot(seq)));
                if (nvs_set_blob(nvs, key, record.get(), sizeof(Crash::Record)) != ESP_OK || nvs_set_u32(nvs, "seq", seq) != ESP_OK ||
                    nvs_commit(nvs) != ESP_OK)
                {
                    Logger::error(TAG, "Couldn't keep crash record %lu in NVS", static_cast<unsigned long>(seq));
                    return false;
                }
                Logger::warn(
                    TAG,
                    "Restarted unexpectedly (%s): crash record %lu kept",
                    Crash::kindName(static_cast<Crash::Kind>(record->kind)),
                    static_cast<unsigned long>(seq));
                return true;
            }

            // Straight into `out` (Preferences::getBytes copies through the stack).
            bool readSlot(nvs_handle_t nvs, size_t slot, Crash::Record &out)
            {
                char key[4];
                snprintf(key, sizeof(key), "r%u", static_cast<unsigned>(slot));
                size_t length = sizeof(Crash::Record);
                return nvs_get_blob(nvs, key, &out, &length) == ESP_OK && Crash::decode(&out, length, out);
            }
        } // namespace

        void arm()
        {
            bootReset = static_cast<ResetCode>(esp_reset_reason());
            const bool powerOn = bootReset == ResetCode::PowerOn;
            pending.reset(new Pending());
            pending->reset = bootReset;
            pending->hasCapture = !powerOn && Crash::intact(capture);
            pending->hasSnapshot = !powerOn && Crash::intact(snap);
            pending->capture = capture;
            pending->snapshot = snap;
            snap.magic = 0; // a crash before the first snapshot of this run has none
            Crash::start(ring, !powerOn);
            pending->lineCount = Crash::copy(ring, pending->lines, Crash::RING_LINES);
            ringReady = true;
            readBuild();
            set_arduino_panic_handler(onPanic, nullptr);
        }

        void keepRecord()
        {
            if (!pending)
                return;
            nvs_handle_t nvs;
            if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
            {
                Logger::error(TAG, "Couldn't open the crash records in NVS");
                pending.reset();
                return;
            }
            if (Crash::kindOf(pending->reset) != Crash::Kind::None)
            {
                uint32_t seq = 0;
                nvs_get_u32(nvs, "seq", &seq); // none yet: 0
                store(nvs, *pending, seq + 1);
            }
            nvs_close(nvs);
            capture.magic = 0; // FR-009: stored (or nothing to store), so never stored twice
            pending.reset();
        }

        void snapshot(const char *roofState, uint32_t roofStackFree)
        {
            static uint32_t last = 0;
            static TaskHandle_t network = nullptr;
            static TaskHandle_t poll = nullptr;
            static TaskHandle_t alerts = nullptr;
            const uint32_t now = millis();
            if (last != 0 && now - last < SNAPSHOT_EVERY_MS)
                return;
            last = now | 1;
            // Looked up until found (each lookup walks the task lists).
            for (auto *entry : {&network, &poll, &alerts})
                if (*entry == nullptr)
                    *entry = xTaskGetHandle(entry == &network ? "async_tcp" : entry == &poll ? "safetyPoll" : "alerts");
            const time_t epoch = time(nullptr);
            snap.magic = 0;
            snap.uptimeMs = uptimeMs();
            snap.epoch = static_cast<int64_t>(epoch) >= Core::CLOCK_VALID_EPOCH ? static_cast<uint32_t>(epoch) : 0;
            snap.freeHeap = ESP.getFreeHeap();
            snap.minFreeHeap = ESP.getMinFreeHeap();
            snap.largestBlock = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            snap.stackLoop = static_cast<uint16_t>(uxTaskGetStackHighWaterMark(nullptr));
            snap.stackRoof = roofStackFree < Crash::STACK_UNKNOWN ? static_cast<uint16_t>(roofStackFree) : Crash::STACK_UNKNOWN;
            snap.stackNetwork = stackLeft(network);
            snap.stackPoll = stackLeft(poll);
            snap.stackAlerts = stackLeft(alerts);
            snap.pad = 0;
            Crash::copyText(snap.roof, sizeof(snap.roof), roofState);
            Crash::seal(snap);
        }

        void logLine(uint8_t level, const char *tag, const char *format, va_list args)
        {
            char text[Crash::LINE_CHARS];
            const int prefix = snprintf(text, sizeof(text), "[%s] ", tag);
            if (prefix > 0 && static_cast<size_t>(prefix) < sizeof(text))
                vsnprintf(text + prefix, sizeof(text) - static_cast<size_t>(prefix), format, args);
            push(uptimeMs(), static_cast<Crash::Level>(level), text);
        }

        void logText(HeapSafe::ChunkText &out)
        {
            if (!ringReady)
                return;
            // The copy is the ring's own size; its lines are read in place.
            const std::unique_ptr<Crash::LogRing> kept = ringCopy();
            if (!kept)
            {
                out.fail();
                return;
            }
            const size_t count = Crash::copy(*kept, kept->lines, Crash::RING_LINES);
            Crash::logText(out, kept->lines, count);
        }

        void crashesText(HeapSafe::ChunkText &out)
        {
            std::unique_ptr<Crash::Record[]> kept(new (std::nothrow) Crash::Record[Crash::KEEP]);
            if (!kept)
            {
                out.fail();
                return;
            }
            size_t count = 0;
            nvs_handle_t nvs;
            if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) // else nothing stored yet
            {
                // Newest first: the slots of the last KEEP sequence numbers.
                uint32_t seq = 0;
                nvs_get_u32(nvs, "seq", &seq);
                for (uint32_t n = seq; n > 0 && seq - n < Crash::KEEP; --n)
                    if (readSlot(nvs, Crash::slot(n), kept[count]) && kept[count].seq == n)
                        ++count;
                nvs_close(nvs);
            }
            Crash::crashesText(out, bootView(), kept.get(), count);
        }

        Crash::BootView bootView()
        {
            Crash::BootView view;
            view.resetCode = static_cast<uint8_t>(bootReset);
            view.bootCount = ::bootCount;
            view.firmware = FIRMWARE_VERSION;
            view.build = build;
            return view;
        }
    } // namespace CrashLog
} // namespace SQM

// Spec 029 FR-002: ESP-IDF calls this from the task watchdog's interrupt,
// before the panic the watchdog raises.
extern "C" void esp_task_wdt_isr_user_handler(void)
{
    SQM::CrashLog::watchdogTasksLength = 0;
    esp_task_wdt_print_triggered_tasks(SQM::CrashLog::appendWatchdogTask, nullptr, nullptr);
}
