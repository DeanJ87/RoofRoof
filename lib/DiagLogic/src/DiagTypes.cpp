#include "DiagTypes.h"

namespace SQM
{
    namespace Diag
    {
        namespace
        {
            float fromHundredths(uint16_t value)
            {
                return value == NONE ? NAN : static_cast<float>(value) / 100.0f;
            }
        } // namespace

        uint32_t epochOf(const Clock &clock, uint32_t ms)
        {
            if (clock.epoch == 0)
                return 0;
            return clock.epoch - (clock.nowMs - ms) / 1000;
        }

        uint16_t toHundredths(float value)
        {
            if (!std::isfinite(value) || value < 0.0f || value > 655.34f)
                return NONE;
            return static_cast<uint16_t>(std::lround(value * 100.0f));
        }

        float pointAmpsAvg(const Point &point)
        {
            return fromHundredths(point.ampsAvg);
        }

        float pointAmpsMax(const Point &point)
        {
            return fromHundredths(point.ampsMax);
        }

        float pointVolts(const Point &point)
        {
            return fromHundredths(point.voltsMin);
        }

        float pointDutyPercent(const Point &point)
        {
            return static_cast<float>(point.duty) * 100.0f / 255.0f;
        }

        uint32_t pointStartMs(const Move &move, uint8_t index)
        {
            return static_cast<uint32_t>(index) * move.intervalMs;
        }

        const char *phaseName(Phase phase)
        {
            switch (phase)
            {
            case Phase::DeadTime:
                return "deadTime";
            case Phase::Slow:
                return "slow";
            case Phase::SoftStart:
                return "softStart";
            case Phase::Cruise:
                return "cruise";
            case Phase::Slowing:
                return "slowing";
            case Phase::Creep:
                return "creep";
            default:
                return "idle";
            }
        }

        const char *resultName(Result result)
        {
            switch (result)
            {
            case Result::Arrived:
                return "arrived";
            case Result::Fault:
                return "fault";
            default:
                return "stopped";
            }
        }

        const char *originName(Roof::Origin origin)
        {
            switch (origin)
            {
            case Roof::Origin::Automatic:
                return "automatic";
            case Roof::Origin::Client:
                return "client";
            case Roof::Origin::Mqtt:
                return "mqtt";
            default:
                return "person";
            }
        }

        const char *directionName(Driver::Command command)
        {
            switch (command)
            {
            case Driver::Command::Forward:
                return "forward";
            case Driver::Command::Reverse:
                return "reverse";
            default:
                return "off";
            }
        }
    } // namespace Diag
} // namespace SQM
