#include "MountClock.h"

#include <cmath>

namespace SQM
{
    namespace Nina
    {
        namespace
        {
            constexpr double MS_PER_HOUR = 3600000.0;
            // Sidereal time gains about 3m56s a day on solar time.
            constexpr double SIDEREAL_PER_SOLAR = 1.00273790935;
            // Exactly the cached value: the same double comes back every time.
            constexpr double SAME_HOURS = 1e-9;

            bool judged(const MountReading &reading)
            {
                return reading.status == ReadingStatus::Good && reading.connected && reading.hasSiderealTime;
            }
        } // namespace

        double siderealStepMs(double fromHours, double toHours)
        {
            double step = std::fmod(toHours - fromHours, 24.0);
            if (step < 0.0)
                step += 24.0;
            return step * MS_PER_HOUR / SIDEREAL_PER_SOLAR;
        }

        ClockChange MountClockWatch::reset()
        {
            const bool was = isFrozen;
            *this = MountClockWatch{};
            return was ? ClockChange::Unjudged : ClockChange::None;
        }

        ClockChange MountClockWatch::restart(const MountReading &reading)
        {
            const bool was = isFrozen;
            known = true;
            baseHours = reading.siderealHours;
            baseAtMs = reading.takenAtMs;
            lastGoodAtMs = reading.takenAtMs;
            isFrozen = false;
            return was ? ClockChange::Unjudged : ClockChange::None;
        }

        ClockChange MountClockWatch::apply(MountReading &reading)
        {
            // A failed read says nothing about the clock; the gap check below
            // catches a long one.
            if (reading.status != ReadingStatus::Good)
                return ClockChange::None;
            // Not connected, or no clock in the reply: as before (spec 004).
            if (!judged(reading))
                return reset();
            const uint64_t at = reading.takenAtMs;
            if (!known || at < lastGoodAtMs || at - lastGoodAtMs > MOUNT_CLOCK_GAP_MS)
                return restart(reading);
            lastGoodAtMs = at;
            const uint64_t elapsedMs = at >= baseAtMs ? at - baseAtMs : 0;
            if (std::fabs(reading.siderealHours - baseHours) >= SAME_HOURS)
            {
                // It moved: live. A jump ahead of the time that passed, or a
                // step back, is a new clock: the baseline restarts on it.
                const bool was = isFrozen;
                const double stepMs = siderealStepMs(baseHours, reading.siderealHours);
                if (stepMs > static_cast<double>(elapsedMs) + MOUNT_CLOCK_TOLERANCE_MS)
                    restart(reading);
                baseHours = reading.siderealHours;
                baseAtMs = at;
                isFrozen = false;
                return was ? ClockChange::Live : ClockChange::None;
            }
            if (elapsedMs < MOUNT_CLOCK_STALE_MS)
                return ClockChange::None;
            const bool was = isFrozen;
            isFrozen = true;
            reading = failedReading(ReadingStatus::Unreadable, MOUNT_FROZEN_TEXT, at);
            reading.frozen = true;
            return was ? ClockChange::None : ClockChange::Frozen;
        }
    } // namespace Nina
} // namespace SQM
