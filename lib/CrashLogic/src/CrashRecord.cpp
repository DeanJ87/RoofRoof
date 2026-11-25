#include "CrashRecord.h"

#include <cstring>

namespace SQM
{
    namespace Crash
    {
        Kind kindOf(ResetCode code)
        {
            switch (code)
            {
            case ResetCode::Panic:
                return Kind::Panic;
            case ResetCode::InterruptWatchdog:
            case ResetCode::TaskWatchdog:
            case ResetCode::OtherWatchdog:
                return Kind::Watchdog;
            case ResetCode::Brownout:
                return Kind::Brownout;
            default:
                return Kind::None;
            }
        }

        const char *kindName(Kind kind)
        {
            switch (kind)
            {
            case Kind::Panic:
                return "panic";
            case Kind::Watchdog:
                return "watchdog";
            case Kind::Brownout:
                return "brownout";
            default:
                return "none";
            }
        }

        const char *resetToken(uint8_t code)
        {
            static const char *const TOKENS[] = {
                "unknown",
                "poweron",
                "external",
                "software",
                "panic",
                "watchdog",
                "watchdog",
                "watchdog",
                "deepsleep",
                "brownout",
                "sdio",
                "usb",
                "jtag",
                "efuse",
                "powerglitch",
                "cpulockup"};
            return code < sizeof(TOKENS) / sizeof(TOKENS[0]) ? TOKENS[code] : "unknown";
        }

        namespace
        {
            void fromLog(const LogLine *log, size_t count, Record &r)
            {
                const size_t first = count > RECORD_LINES ? count - RECORD_LINES : 0;
                for (size_t i = first; log != nullptr && i < count; ++i)
                {
                    RecordLine &line = r.log[r.logCount++];
                    line.ms = log[i].ms;
                    line.level = log[i].level;
                    copyText(line.text, sizeof(line.text), log[i].text);
                }
            }
        } // namespace

        bool build(const RecordInputs &in, Record &out)
        {
            const Kind kind = kindOf(in.reset);
            if (kind == Kind::None)
                return false;
            Record r;
            std::memset(&r, 0, sizeof(r)); // padding too: the blob is compared and stored as is
            r.version = RECORD_VERSION;
            r.kind = static_cast<uint8_t>(kind);
            r.resetCode = static_cast<uint8_t>(in.reset);
            r.seq = in.seq;
            copyText(r.firmware, sizeof(r.firmware), in.firmware);
            copyText(r.build, sizeof(r.build), in.build);
            if (in.capture != nullptr)
            {
                r.flags |= Record::CAPTURED;
                r.capture = *in.capture;
                if (r.capture.frames > MAX_FRAMES)
                    r.capture.frames = MAX_FRAMES;
                r.uptimeMs = r.capture.uptimeMs;
            }
            if (in.snapshot != nullptr)
            {
                r.flags |= Record::SNAPSHOT;
                r.snapshot = *in.snapshot;
                if (in.capture == nullptr)
                    r.uptimeMs = r.snapshot.uptimeMs;
                if (r.snapshot.epoch != 0)
                {
                    // The crash came after the snapshot (by up to about a second).
                    const uint32_t since = r.uptimeMs >= r.snapshot.uptimeMs ? r.uptimeMs - r.snapshot.uptimeMs : 0;
                    r.epoch = r.snapshot.epoch + since / 1000;
                    r.flags |= Record::TIMED;
                }
            }
            fromLog(in.log, in.logCount, r);
            out = r;
            return true;
        }

        bool decode(const void *bytes, size_t length, Record &out)
        {
            if (bytes == nullptr || length != sizeof(Record))
                return false;
            Record r;
            std::memcpy(&r, bytes, sizeof(r));
            const bool known = r.kind >= static_cast<uint8_t>(Kind::Panic) && r.kind <= static_cast<uint8_t>(Kind::Brownout);
            if (r.version != RECORD_VERSION || !known || r.capture.frames > MAX_FRAMES || r.logCount > RECORD_LINES)
                return false;
            out = r;
            return true;
        }
    } // namespace Crash
} // namespace SQM
