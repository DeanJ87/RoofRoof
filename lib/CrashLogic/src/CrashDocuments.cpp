#include "CrashDocuments.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace SQM
{
    namespace Crash
    {
        namespace
        {
            // One formatted line (cut at 200 characters) and a newline.
            __attribute__((format(printf, 2, 3))) void line(std::string &out, const char *format, ...)
            {
                char text[200];
                va_list args;
                va_start(args, format);
                vsnprintf(text, sizeof(text), format, args);
                va_end(args);
                out += text;
                out += '\n';
            }

            unsigned long ul(uint32_t value)
            {
                return static_cast<unsigned long>(value);
            }

            // A stack's headroom, or "?" when it couldn't be read.
            const char *stack(char (&text)[8], uint16_t value)
            {
                if (value == STACK_UNKNOWN)
                    return "?";
                snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
                return text;
            }

            void writeTime(std::string &out, const Record &r)
            {
                if ((r.flags & Record::TIMED) == 0)
                {
                    line(out, "Time: unknown (clock not set), %lu s after start", ul(r.uptimeMs / 1000));
                    return;
                }
                const time_t epoch = static_cast<time_t>(r.epoch);
                struct tm utc;
                gmtime_r(&epoch, &utc);
                char when[24];
                strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &utc);
                line(out, "Time: %s UTC, %lu s after start", when, ul(r.uptimeMs / 1000));
            }

            void writeCapture(std::string &out, const Record &r)
            {
                if ((r.flags & Record::CAPTURED) == 0)
                {
                    line(out, "Reason: not captured");
                    return;
                }
                const Capture &c = r.capture;
                char left[8];
                line(out, "Reason: %s", c.reason);
                line(out, "Task: %s, core %u, stack left %s B", c.task, static_cast<unsigned>(c.core), stack(left, c.taskStackFree));
                if (c.watchdogTasks[0] != '\0')
                    line(out, "Stuck tasks: %s", c.watchdogTasks);
                if ((c.flags & Capture::FLASH_WRITE) != 0)
                    line(out, "Flash write: in progress");
                // The PC and the frames as tools/decode_crash.py and a serial monitor's decoder take them.
                out += "Backtrace:";
                char address[12];
                snprintf(address, sizeof(address), " 0x%08lx", ul(c.pc));
                out += address;
                for (size_t i = 0; i < c.frames && i < MAX_FRAMES; ++i)
                {
                    snprintf(address, sizeof(address), " 0x%08lx", ul(c.backtrace[i]));
                    out += address;
                }
                line(
                    out,
                    "%s%s",
                    (c.flags & Capture::CORRUPT) != 0 ? " [corrupt]" : "",
                    (c.flags & Capture::CONTINUES) != 0 ? " [continues]" : "");
            }

            void writeSnapshot(std::string &out, const Record &r)
            {
                if ((r.flags & Record::SNAPSHOT) == 0)
                    return;
                const Snapshot &s = r.snapshot;
                char a[8], b[8], c[8], d[8], e[8];
                line(out, "Memory: free %lu B, lowest %lu B, largest block %lu B", ul(s.freeHeap), ul(s.minFreeHeap), ul(s.largestBlock));
                line(
                    out,
                    "Stack left: loop %s, roof %s, async_tcp %s, safetyPoll %s, alerts %s B",
                    stack(a, s.stackLoop),
                    stack(b, s.stackRoof),
                    stack(c, s.stackNetwork),
                    stack(d, s.stackPoll),
                    stack(e, s.stackAlerts));
                line(out, "Roof: %.*s", static_cast<int>(sizeof(s.roof)), s.roof);
            }

            const char *levelText(uint8_t level)
            {
                static const char *const LEVELS[] = {"DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};
                return level < 5 ? LEVELS[level] : LEVELS[1];
            }
        } // namespace

        void writeRecord(std::string &out, const Record &r)
        {
            line(out, "Record %lu: %s", ul(r.seq), kindName(static_cast<Kind>(r.kind)));
            writeTime(out, r);
            line(out, "Reset reason: %s (%u)", resetToken(r.resetCode), static_cast<unsigned>(r.resetCode));
            line(out, "Firmware: %s, build %s", r.firmware[0] != '\0' ? r.firmware : "?", r.build[0] != '\0' ? r.build : "?");
            writeCapture(out, r);
            writeSnapshot(out, r);
            if (r.logCount == 0)
                return;
            line(out, "Log before the restart:");
            for (size_t i = 0; i < r.logCount && i < RECORD_LINES; ++i)
                line(out, "  %lu.%03lus %s %s", ul(r.log[i].ms / 1000), ul(r.log[i].ms % 1000), levelText(r.log[i].level), r.log[i].text);
        }

        std::string crashesText(const BootView &boot, const Record *records, size_t count)
        {
            std::string out;
            line(
                out,
                "This start: %s (%u), boot %lu",
                resetToken(boot.resetCode),
                static_cast<unsigned>(boot.resetCode),
                ul(boot.bootCount));
            line(out, "Running: %s, build %s", boot.firmware, boot.build[0] != '\0' ? boot.build : "?");
            line(out, "Records: %u (the last %u are kept)", static_cast<unsigned>(count), static_cast<unsigned>(KEEP));
            for (size_t i = 0; i < count; ++i)
            {
                out += '\n';
                writeRecord(out, records[i]);
            }
            return out;
        }

        std::string logText(const LogLine *lines, size_t count)
        {
            std::string out;
            for (size_t i = 0; i < count; ++i)
                line(
                    out,
                    "#%u %lu.%03lus %s %s",
                    static_cast<unsigned>(lines[i].boot),
                    ul(lines[i].ms / 1000),
                    ul(lines[i].ms % 1000),
                    levelText(lines[i].level),
                    lines[i].text);
            return out;
        }
    } // namespace Crash
} // namespace SQM
