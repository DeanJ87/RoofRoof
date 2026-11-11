#include "ParkRule.h"

#include "SafetyInput.h"

namespace SQM
{
    namespace Safety
    {
        namespace
        {
            using Nina::ReadingStatus;

            uint32_t statusReason(ReadingStatus status)
            {
                switch (status)
                {
                case ReadingStatus::NotConfigured:
                    return ParkNinaNotConfigured;
                case ReadingStatus::Waiting:
                    return ParkNinaWaiting;
                case ReadingStatus::Unreachable:
                    return ParkNinaUnreachable;
                case ReadingStatus::Unreadable:
                    return ParkNinaUnreadable;
                case ReadingStatus::ApiError:
                    return ParkNinaApiError;
                case ReadingStatus::Good:
                    break;
                }
                return 0;
            }

            // N.I.N.A.'s reasons; `usable` is set when the reading is good and fresh.
            uint32_t ninaReasons(const ParkInputs &in, uint32_t ageMs, bool &usable)
            {
                usable = false;
                const uint32_t status = statusReason(in.nina.status);
                if (status != 0)
                    return status;
                if (ageMs > in.ninaMaxAgeMs)
                    return ParkNinaStale;
                usable = true;
                if (!in.nina.connected)
                    return ParkMountNotConnected;
                uint32_t reasons = 0;
                if (!in.nina.atPark)
                    reasons |= ParkMountNotAtPark;
                if (in.nina.slewing)
                    reasons |= ParkMountSlewing;
                return reasons;
            }

            uint32_t switchReasons(SwitchReading reading, uint32_t openBit, uint32_t unreadableBit)
            {
                if (reading == SwitchReading::Open)
                    return openBit;
                if (reading == SwitchReading::Unreadable)
                    return unreadableBit;
                return 0;
            }

            // Fitted switches: (all closed, any open). An unreadable switch is neither.
            void switchSummary(const ParkInputs &in, bool &allClosed, bool &anyOpen)
            {
                const SwitchReading readings[] = {in.switch1, in.switch2};
                allClosed = true;
                anyOpen = false;
                for (uint8_t i = 0; i < in.fittedSwitches && i < 2; ++i)
                {
                    allClosed = allClosed && readings[i] == SwitchReading::Closed;
                    anyOpen = anyOpen || readings[i] == SwitchReading::Open;
                }
            }

            // Spec 004 FR-014: a usable, connected N.I.N.A. reading and the fitted
            // switches point opposite ways.
            bool disagree(const ParkInputs &in, bool ninaUsable)
            {
                if (!ninaUsable || !in.nina.connected || in.fittedSwitches == 0)
                    return false;
                bool allClosed = false;
                bool anyOpen = false;
                switchSummary(in, allClosed, anyOpen);
                return in.nina.atPark ? anyOpen : allClosed;
            }

            void append(std::string &text, bool when, const std::string &line)
            {
                if (!when)
                    return;
                text += text.empty() ? "" : "; ";
                text += line;
            }

            std::string ninaFailure(const ParkResult &r, const char *what)
            {
                return std::string("N.I.N.A. ") + what + (r.ninaDetail.empty() ? "" : ": " + r.ninaDetail);
            }
        } // namespace

        ParkResult evaluatePark(const ParkInputs &in)
        {
            ParkResult r;
            r.ninaMaxAgeMs = in.ninaMaxAgeMs;
            r.ninaAgeMs = elapsedMs(in.nina.takenAtMs, in.nowMs);
            r.ninaDetail = in.nina.detail;
            bool ninaUsable = false;
            r.reasons = ninaReasons(in, r.ninaAgeMs, ninaUsable);
            if (in.fittedSwitches >= 1)
                r.reasons |= switchReasons(in.switch1, ParkSwitch1Open, ParkSwitch1Unreadable);
            if (in.fittedSwitches >= 2)
                r.reasons |= switchReasons(in.switch2, ParkSwitch2Open, ParkSwitch2Unreadable);
            r.disagree = disagree(in, ninaUsable);
            if (r.disagree)
                r.reasons |= ParkSourcesDisagree;
            r.parked = r.reasons == 0;
            return r;
        }

        std::string describe(const ParkResult &r)
        {
            if (r.parked)
                return "parked";
            const uint32_t b = r.reasons;
            std::string text;
            append(text, b & ParkNinaNotConfigured, "N.I.N.A. not configured");
            append(text, b & ParkNinaWaiting, "N.I.N.A. not read yet");
            append(text, b & ParkNinaUnreachable, ninaFailure(r, "not responding"));
            append(text, b & ParkNinaUnreadable, ninaFailure(r, "reply unreadable"));
            append(text, b & ParkNinaApiError, ninaFailure(r, "error"));
            append(
                text,
                b & ParkNinaStale,
                "N.I.N.A. stale: reading " + formatSeconds(r.ninaAgeMs) + " old > " + formatSeconds(r.ninaMaxAgeMs));
            append(text, b & ParkMountNotConnected, "Mount not connected in N.I.N.A.");
            append(text, b & ParkMountNotAtPark, "N.I.N.A. says mount not at park");
            append(text, b & ParkMountSlewing, "Mount slewing");
            append(text, b & ParkSwitch1Open, "Park switch 1 open");
            append(text, b & ParkSwitch1Unreadable, "Park switch 1 unreadable");
            append(text, b & ParkSwitch2Open, "Park switch 2 open");
            append(text, b & ParkSwitch2Unreadable, "Park switch 2 unreadable");
            append(text, b & ParkSourcesDisagree, "N.I.N.A. and the park switches disagree");
            return "not parked: " + text;
        }
    } // namespace Safety
} // namespace SQM
