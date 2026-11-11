#include "RoofText.h"

#include <cmath>
#include <cstdio>

#include "RoofSettings.h"

namespace SQM
{
    namespace RoofIo
    {
        namespace
        {
            // printf into a std::string (the texts are short).
            template <typename... Args> std::string format(const char *pattern, Args... args)
            {
                char buffer[160];
                std::snprintf(buffer, sizeof(buffer), pattern, args...);
                return buffer;
            }

            double seconds(float ms)
            {
                return static_cast<double>(ms) / 1000.0;
            }

            const char *endText(float end)
            {
                return end >= 0.5f ? "open" : "closed";
            }

            std::string supplyText(float measured, float limit)
            {
                if (!std::isfinite(measured))
                    return format("Supply unreadable (minimum %.1f V)", static_cast<double>(limit));
                return format("Supply %.1f V < %.1f V", static_cast<double>(measured), static_cast<double>(limit));
            }

            // "park not confirmed, park switch 1 not closed": the inputs that fail the park.
            std::string failedParkInputs(uint32_t bits)
            {
                std::string text;
                auto add = [&](uint32_t bit, const char *what)
                {
                    if ((bits & bit) == 0)
                        return;
                    text += text.empty() ? "" : ", ";
                    text += what;
                };
                add(InterlockParkResult, "park not confirmed");
                add(InterlockSwitch1, "park switch 1 not closed");
                add(InterlockSwitch2, "park switch 2 not closed");
                return text.empty() ? "park not confirmed" : text;
            }

            // Spec 011: the bench firmware saw a real supply (or none it could read).
            std::string benchSupplyText(float volts, float limitVolts)
            {
                const double limit = static_cast<double>(limitVolts);
                const double measured = static_cast<double>(volts);
                if (!std::isfinite(measured))
                    return format("Bench firmware on a powered roof? Supply input unreadable (limit %.1f V)", limit);
                return format("Bench firmware on a powered roof: supply %.1f V >= %.1f V", measured, limit);
            }

            std::string externalText(const FaultRecord &r)
            {
                switch (r.cause)
                {
                case FaultCause::Watchdog:
                    return format(
                        "Control step %.0f ms late > %.0f ms (watchdog)",
                        static_cast<double>(r.fault.measured),
                        static_cast<double>(r.fault.limit));
                case FaultCause::ParkInterlock:
                    return "Park lost during the close: " + failedParkInputs(static_cast<uint32_t>(r.fault.measured));
                case FaultCause::WatchdogStopped:
                    return format(
                        "Watchdog timer stopped: last check %.0f ms ago > %.0f ms",
                        static_cast<double>(r.fault.measured),
                        static_cast<double>(r.fault.limit));
                case FaultCause::StoredUnreadable:
                    return "Stored fault unreadable: the fault saved before the restart couldn't be read back";
                case FaultCause::BenchSupply:
                    return benchSupplyText(r.fault.measured, r.fault.limit);
                default:
                    return format("Fault raised: %g (limit %g)", static_cast<double>(r.fault.measured), static_cast<double>(r.fault.limit));
                }
            }

            std::string motionFaultText(const Roof::Fault &f)
            {
                const double measured = static_cast<double>(f.measured);
                const double limit = static_cast<double>(f.limit);
                switch (f.kind)
                {
                case Roof::FaultKind::Timeout:
                    return format("No target magnet after %.1f s >= %.1f s timeout", seconds(f.measured), seconds(f.limit));
                case Roof::FaultKind::DidNotLeave:
                    return format("Still on the start magnet after %.1f s >= %.1f s", seconds(f.measured), seconds(f.limit));
                case Roof::FaultKind::WrongMagnet:
                    return format("Wrong magnet: read the %s magnet moving to %s", endText(f.measured), endText(f.limit));
                case Roof::FaultKind::HallRail:
                    return format("Hall sensor at a rail: %.0f counts %s %.0f", measured, measured <= limit ? "<=" : ">=", limit);
                case Roof::FaultKind::OverCurrent:
                    return format("Motor current %.1f A >= %.1f A", measured, limit);
                case Roof::FaultKind::CurrentUnreadable:
                    return format("Motor current unreadable (limit %.1f A)", limit);
                case Roof::FaultKind::SupplyLow:
                    return supplyText(f.measured, f.limit);
                case Roof::FaultKind::LateStep:
                    return format("Control step %.0f ms late > %.0f ms", measured, limit);
                case Roof::FaultKind::DriveBudget:
                    return format("Driven %.1f s without reaching a magnet >= %.1f s budget", seconds(f.measured), seconds(f.limit));
                default:
                    return format("Fault raised: %g (limit %g)", measured, limit);
                }
            }
        } // namespace

        std::string describeSupply(float volts, float minimum)
        {
            return supplyText(Roof::supplyReadable(volts) ? volts : NAN, minimum);
        }

        std::string describeFault(const FaultRecord &record)
        {
            if (record.fault.kind == Roof::FaultKind::None)
                return "";
            if (record.fault.kind == Roof::FaultKind::External)
                return externalText(record);
            return motionFaultText(record.fault);
        }

        std::string describeRefusal(const Roof::CommandResult &result)
        {
            const double limit = static_cast<double>(result.limit);
            switch (result.reason)
            {
            case Roof::Refusal::None:
                return "";
            case Roof::Refusal::FaultLatched:
                return "A fault is latched: a person must clear it first";
            case Roof::Refusal::HallNotSettled:
                return "Hall reading not settled yet: try again in a moment";
            case Roof::Refusal::PositionUnknown:
                return "Position unknown: only a person can move the roof";
            case Roof::Refusal::SupplyLow:
            case Roof::Refusal::SupplyUnreadable:
                return supplyText(result.reason == Roof::Refusal::SupplyUnreadable ? NAN : result.measured, result.limit);
            case Roof::Refusal::CurrentUnreadable:
                return format("Motor current unreadable (limit %.1f A)", limit);
            case Roof::Refusal::SettingsInvalid:
                return "Roof settings invalid: fix them on the settings page";
            case Roof::Refusal::NotAPerson:
                return "Only a person can clear a fault";
            case Roof::Refusal::Moving:
                return "The roof is moving";
            }
            return "Refused";
        }

        std::string describeGate(GateRefusal refusal, const GateInputs &in)
        {
            switch (refusal)
            {
            case GateRefusal::WeatherNotSafe:
                return "Weather not safe: " + (in.weatherText.empty() ? std::string("no verdict yet") : in.weatherText);
            case GateRefusal::MountNotParked:
                return "Mount not parked: " + failedParkInputs(interlockBits(in)) +
                       (in.parked || in.parkText.empty() ? "" : " (" + in.parkText + ")");
            default:
                return "";
            }
        }
    } // namespace RoofIo
} // namespace SQM
