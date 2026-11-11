#include "CloseSequence.h"

// Spec 013: the daylight-only episode (research R4, data-model.md). Kept
// apart from spec 005's transition table in CloseSequence.cpp.

namespace SQM
{
    namespace CloseSeq
    {
        namespace
        {
            uint64_t elapsed(uint64_t sinceMs, uint64_t nowMs)
            {
                return nowMs > sinceMs ? nowMs - sinceMs : 0;
            }
        } // namespace

        // Spec 013 (research R4). An episode is daylight-only from the idle
        // step that starts it with the weather safe until the weather first
        // stops being safe. At that moment a daylight-only stand-down (a
        // person's stop, a halted close) or back-off must not hold the
        // weather close back: the sequence re-arms and the weather attempt
        // starts on this same step, as if the daylight episode hadn't been.
        // A person's stop seen on this very step wins (it stands down below).
        // A person's stand-down waits for the weather to have been not safe
        // for WEATHER_REARM_CONFIRM_MS (a new stop restarts the wait); a halt
        // or a back-off re-arms at once. The critical alerts' throttle starts
        // afresh, so the weather close's failures are announced in their own
        // words even when the daylight close failed the same way just before.
        void Sequence::trackEpisode(const Inputs &given, bool personStopped, bool restoredNow)
        {
            const bool waitOver = personWaitOver(given, personStopped);
            if (current == Phase::Idle)
            {
                // After a daylight-only episode (ended by the sun setting, the
                // mode changed, or 10 minutes without a reason), the weather's
                // own critical alerts go out in their own words.
                if (afterDaylightEpisode && !given.weatherSafe)
                {
                    clearAlertThrottle();
                    afterDaylightEpisode = false;
                }
                daylightOnly = given.daylight && given.weatherSafe;
                return;
            }
            // The first step after a restart raises the record's reminder
            // first (a restart during a close): the weather re-arms next step.
            if (given.weatherSafe || !daylightOnly || restoredNow)
                return;
            if (current == Phase::HeldByPerson && !waitOver)
                return;
            daylightOnly = false;
            clearAlertThrottle();
            // CannotMove too: its "can't close by itself" is raised again at
            // once, in the weather's words, with the fresh throttle.
            const bool heldBack =
                current == Phase::HeldByPerson || current == Phase::Halted || current == Phase::RetryWait || current == Phase::CannotMove;
            if (heldBack && !personStopped)
                enter(Phase::Idle);
        }

        // A person's daylight stand-down: the weather has been not safe for
        // WEATHER_REARM_CONFIRM_MS in all since the stand-down began, and no
        // new stop (which starts the count again).
        bool Sequence::personWaitOver(const Inputs &given, bool personStopped)
        {
            const bool unsafe = !given.weatherSafe;
            if (current != Phase::HeldByPerson || personStopped)
                heldUnsafeMs = 0;
            else if (unsafe && lastWeatherUnsafe)
                heldUnsafeMs += elapsed(lastWeatherMs, given.nowMs);
            lastWeatherUnsafe = unsafe;
            lastWeatherMs = given.nowMs;
            return heldUnsafeMs >= WEATHER_REARM_CONFIRM_MS && !personStopped;
        }

        void Sequence::clearAlertThrottle()
        {
            for (auto &row : alertedEver)
                for (bool &ever : row)
                    ever = false;
        }
    } // namespace CloseSeq
} // namespace SQM
