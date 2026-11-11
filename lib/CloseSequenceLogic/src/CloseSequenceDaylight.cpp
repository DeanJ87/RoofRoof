#include "CloseSequence.h"

#include <algorithm>

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
        // A person's stand-down and a halt wait for the weather to have been
        // not safe for WEATHER_REARM_CONFIRM_MS in all (a new stop restarts
        // the count); a back-off or a can't-move re-arms at once. The critical alerts' throttle starts
        // afresh, so the weather close's failures are announced in their own
        // words even when the daylight close failed the same way just before.
        void Sequence::trackEpisode(const Inputs &given, bool personStopped, bool restoredNow)
        {
            const bool waitOver = personWaitOver(given, personStopped);
            const bool weatherSafeLong = weatherSafeRun.lasted(given.weatherSafe, given.nowMs, REARM_SAFE_MS);
            if (current == Phase::Idle)
            {
                trackIdle(given);
                return;
            }
            // A weather episode the weather has left for REARM_SAFE_MS, kept
            // going only by daylight: from here on it is daylight's (spec 005
            // would have ended it), so new unsafe weather is a new weather
            // episode with its own alerts and re-arm (as below).
            if (!daylightOnly && weatherSafeLong && given.daylight)
            {
                daylightOnly = true;
                heldUnsafeMs = 0;
                return;
            }
            // The first step after a restart raises the record's reminder
            // first (a restart during a close): the weather re-arms next step.
            // A person's stop on this very step stands the daylight episode
            // down as daylight's: the weather then re-arms it after its 30 s.
            if (given.weatherSafe || !daylightOnly || restoredNow || personStopped)
                return;
            if ((current == Phase::HeldByPerson || current == Phase::Halted) && !waitOver)
                return;
            daylightOnly = false;
            handToWeather();
            // CannotMove too: its "can't close by itself" is raised again at
            // once, in the weather's words, with the fresh throttle.
            if (heldBack(current))
                enter(Phase::Idle);
        }

        // As if the daylight episode hadn't been: the weather's critical
        // alerts go out in their own words, and a daylight close's failure
        // doesn't hold the weather's close back.
        bool Sequence::heldBack(Phase phase)
        {
            return phase == Phase::HeldByPerson || phase == Phase::Halted || phase == Phase::RetryWait || phase == Phase::CannotMove;
        }

        uint64_t Sequence::daylightCount() const
        {
            return daylightOnly ? heldUnsafeMs : 0;
        }

        void Sequence::handToWeather()
        {
            clearAlertThrottle();
            if (closeFailedInDaylight)
                closeFailedOnce = false;
            requested = false; // the weather's park request goes out at once, as in spec 005
            // A daylight attempt waiting for the park carries on as the
            // weather's own attempt: its full time limit, from now.
            if (current == Phase::WaitPark)
            {
                phaseSinceMs = lastNowMs;
                phaseWallStartMs = lastNowMs;
                attemptLimitMs = parkTimeoutMs;
                parkRefusals = 0;
            }
        }

        // Idle: a new episode is daylight-only when it starts with the weather
        // safe. After a daylight-only episode (ended by the sun setting, the
        // mode changed, or 10 minutes without a reason), the weather's own
        // critical alerts go out in their own words.
        void Sequence::trackIdle(const Inputs &given)
        {
            // After a daylight-only episode (ended by the sun setting, the
            // mode changed, or 10 minutes without a reason), the weather's
            // own critical alerts go out in their own words.
            if (afterDaylightEpisode && !given.weatherSafe)
            {
                handToWeather();
                paused.has = false; // a fresh weather attempt, not the daylight one's back-off
                afterDaylightEpisode = false;
            }
            daylightOnly = given.daylight && given.weatherSafe;
        }

        // A person's daylight stand-down or a halt: the weather has been not
        // safe for WEATHER_REARM_CONFIRM_MS in all since it began (each step
        // that follows an unsafe one counts), and no new stop (which starts
        // the count again).
        bool Sequence::personWaitOver(const Inputs &given, bool personStopped)
        {
            const bool unsafe = !given.weatherSafe;
            if ((current != Phase::HeldByPerson && current != Phase::Halted) || personStopped)
                heldUnsafeMs = 0;
            else if (lastWeatherUnsafe)
                // At most MAX_COUNT_STEP_MS per step: a stalled loop after one
                // stale poll can't fill the count by itself.
                heldUnsafeMs += std::min<uint64_t>(elapsed(lastWeatherMs, given.nowMs), MAX_COUNT_STEP_MS);
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
