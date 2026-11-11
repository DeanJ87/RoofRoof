#include "MotionGuards.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "StepWatchdog.h"

namespace SQM
{
    namespace RoofIo
    {
        bool roofMoving(Roof::State state)
        {
            return state == Roof::State::Opening || state == Roof::State::Closing;
        }

        bool platformActionRefused(Roof::State state)
        {
            return roofMoving(state);
        }

        bool flashBusyRefuses(Roof::Command command, bool flashBusy)
        {
            return flashBusy && (command == Roof::Command::Open || command == Roof::Command::Close);
        }

        bool commandHeaderValid(const char *value)
        {
            return value != nullptr && std::strcmp(value, "1") == 0;
        }

        bool watchdogAlive(uint32_t nowMs, uint32_t beatMs, uint32_t limitMs)
        {
            return stepGapMs(nowMs, beatMs) <= limitMs;
        }

        float runningAmps(float readAmps, const Roof::Status &status, const Roof::Output &applied)
        {
            if (!std::isfinite(readAmps))
                return readAmps;
            const bool on = applied.command != Driver::Command::Off && applied.duty > 0;
            if (!on)
                return readAmps; // motor off: the reading is the sense output's offset
            const float amps = readAmps * 255.0f / static_cast<float>(applied.duty);
            const bool settled = status.driving && applied.duty >= CURRENT_CHECK_MIN_DUTY && status.moveElapsedMs >= CURRENT_SETTLE_MS;
            if (settled && amps < MIN_RUNNING_AMPS)
                return std::numeric_limits<float>::quiet_NaN();
            return amps;
        }
    } // namespace RoofIo
} // namespace SQM
