#include "PollSchedule.h"

namespace SQM
{
    namespace Safety
    {
        uint64_t PollSchedule::sqmeterNextMs(uint64_t nowMs, bool moving) const
        {
            if (!sqmeterEver)
                return nowMs;
            const uint64_t next = sqmeterStartMs + (moving ? SQMETER_MOVING_POLL_MS : SQMETER_POLL_MS);
            return next < nowMs ? nowMs : next;
        }

        PollPass PollSchedule::plan(uint64_t nowMs, bool ninaOn, bool sqmeterOn, bool moving) const
        {
            PollPass pass;
            pass.sqmeter = sqmeterOn && nowMs >= sqmeterNextMs(nowMs, moving);
            if (!ninaOn)
                return pass;
            const bool due = !ninaEver || nowMs - ninaStartMs >= NINA_POLL_MS;
            // Before a weather poll that could take its whole SQMETER_TOTAL_MS.
            const bool beforeWeather = pass.sqmeter && nowMs - ninaEndMs >= NINA_BEFORE_WEATHER_MS;
            pass.nina = due || beforeWeather;
            return pass;
        }

        void PollSchedule::ninaRead(uint64_t startedMs, uint64_t endedMs)
        {
            ninaEver = true;
            ninaStartMs = startedMs;
            ninaEndMs = endedMs;
        }

        void PollSchedule::sqmeterPolled(uint64_t startedMs)
        {
            sqmeterEver = true;
            sqmeterStartMs = startedMs;
        }
    } // namespace Safety
} // namespace SQM
