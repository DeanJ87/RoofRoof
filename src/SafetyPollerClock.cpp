#include "Logger.h"
#include "SafetyPoller.h"

// Spec 051 (finding F60): N.I.N.A. can go on answering mount/info with its last
// known values after the mount's link has died (Connected true, AtPark frozen).
// A reading whose SiderealTime has stopped is marked frozen here, before anything
// else sees it, so the park check, the park request (spec 050), the N.I.N.A.-lost
// close and its alerts all treat it as unreadable. The rule is lib/NinaLogic's
// (MountClock).

namespace SQM
{
    namespace
    {
        constexpr const char *TAG = "Safety";

        void logChange(Nina::ClockChange change)
        {
            switch (change)
            {
            case Nina::ClockChange::Frozen:
                Logger::warn(
                    TAG,
                    "%s: SiderealTime unchanged for %u s while N.I.N.A. answers. Park unknown until it moves again",
                    Nina::MOUNT_FROZEN_TEXT,
                    static_cast<unsigned>(Nina::MOUNT_CLOCK_STALE_MS / 1000));
                break;
            case Nina::ClockChange::Live:
                Logger::info(TAG, "N.I.N.A.'s mount values are updating again (SiderealTime moves): the park reading counts again");
                break;
            case Nina::ClockChange::Unjudged:
                Logger::info(TAG, "N.I.N.A.'s mount values are no longer judged frozen (no mount clock to check, or the check restarted)");
                break;
            case Nina::ClockChange::None:
                break;
            }
        }
    } // namespace

    void SafetyPoller::watchClock(Nina::MountReading &reading, uint32_t generation)
    {
        if (generation != clockGeneration)
        {
            clockGeneration = generation;
            logChange(mountClock.reset()); // new settings: the check starts again
        }
        logChange(mountClock.apply(reading));
    }
} // namespace SQM
