// Spec 013 SC-003: spec 005's property test (test_close_properties, P1-P11)
// with daylight as a second reason to close, changing at random alongside the
// weather; "unsafe" there reads "a close is wanted" here. Also:
//   D1  the weather turning unsafe during a daylight-only halt or back-off
//       (no person's stop that step) leaves it on the same step; a person's
//       stand-down once the weather has been not safe for 30 s;
//   D2  every decision's episode (daylight-only or not) is the observer's;
//   D3  a stop or halt never ends while daylight alone still wants a close.
// A failure names the seed and the step, so it can be replayed.

#include <algorithm>
#include <cstdio>
#include <unity.h>

#include "CloseSequence.h"
#include "Config.h" // for the library finder: RoofLogic (Roof::State) builds with ConfigModel
#include "World.h"

using namespace SQM;
using namespace SQM::CloseSeq;
using Roof::State;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t SEQUENCES = 5000;
    constexpr int STEPS_PER_SEQUENCE = 3000;

    // Updates the observer's runs for this step's inputs (before the decision).
    void observeInputs(Watch &watch, const Inputs &in)
    {
        watch.closedByMove = watch.lastRoof == State::Closing && in.roof == State::Closed;
        watch.lastRoof = in.roof;
        if (!in.parked)
            watch.parkedRun = false;
        else if (!watch.parkedRun)
        {
            watch.parkedRun = true;
            watch.parkedSinceMs = in.nowMs;
        }
        if (wanted(in))
        {
            // A safe spell of 10 min or more (seen only when it ends, between steps) starts afresh.
            if (watch.safeRun && in.nowMs - watch.safeSinceMs >= REARM_SAFE_MS)
                watch.quietUnsafeMs = 0;
            watch.safeRun = false;
        }
        else if (!watch.safeRun)
        {
            watch.safeRun = true;
            watch.safeSinceMs = in.nowMs;
        }
        if (in.roof == State::Closed || (watch.safeRun && in.nowMs - watch.safeSinceMs >= REARM_SAFE_MS))
        {
            watch.held = false;
            watch.halted = false;
        }
    }

    bool running(Phase phase)
    {
        return phase == Phase::WaitPark || phase == Phase::Closing || phase == Phase::RetryWait;
    }

    // One decision with the phases around it.
    struct Seen
    {
        Decision decision;
        Phase before = Phase::Idle;
        Phase after = Phase::Idle;
    };

    // P2: a close only when it is allowed. nullptr when it holds.
    const char *checkClose(const Inputs &in, const Watch &watch)
    {
        if (!wanted(in))
            return "P2 close while nothing wants one";
        if (!watch.parkedRun || in.nowMs - watch.parkedSinceMs < PARK_SETTLE_MS)
            return "P2 close without 4 s of park";
        if (!movable(in.roof) || in.closePending)
            return "P2 close of a roof that isn't movable";
        if (in.nowMs < watch.closeBarUntilMs)
            return "P10 a close within the back-off after a refused or interrupted close";
        return nullptr;
    }

    // P3: park requests spaced and only while not safe.
    const char *checkRequest(const Inputs &in, const Watch &watch)
    {
        if (!wanted(in))
            return "P3 park request while nothing wants a close";
        if (watch.anyRequest && in.nowMs - watch.lastRequestMs < PARK_REQUEST_GAP_MS)
            return "P3 park requests closer than 30 s";
        return nullptr;
    }

    // P1-P5 and P8's stop rule for one decision.
    const char *checkAction(const Seen &seen, const Inputs &in, const Watch &watch)
    {
        const Action action = seen.decision.action;
        if (action == Action::None)
            return nullptr;
        if (action != Action::RequestPark && action != Action::Close && action != Action::Stop)
            return "P1 an action other than request, close or stop";
        if (watch.held)
            return "P4 an action while stood down by a person";
        if (watch.halted)
            return "P9 an action after a close outlasted its guard";
        if (in.roof == State::Unknown || in.roof == State::Error)
            return "P5 an action while unknown or faulted";
        if (action == Action::Close)
            return checkClose(in, watch);
        if (action == Action::RequestPark)
            return checkRequest(in, watch);
        return seen.before == Phase::Closing ? nullptr : "P8 a stop outside a close";
    }

    // P6: an attempt never outlasts its park limit.
    const char *checkAttempt(const Seen &seen, const Inputs &in, Watch &watch)
    {
        if (seen.decision.action == Action::RequestPark)
        {
            watch.anyRequest = true;
            watch.lastRequestMs = in.nowMs;
        }
        if (seen.after == Phase::WaitPark && seen.before != Phase::WaitPark && seen.decision.event == Event::Started)
        {
            watch.attemptOn = true;
            watch.attemptSinceMs = in.nowMs;
        }
        if (seen.after != Phase::WaitPark && seen.after != Phase::Closing)
            watch.attemptOn = false;
        // A parked mount held back only by the back-off after a failed close isn't a park failure.
        const bool heldBack = watch.parkedRun && in.nowMs - watch.parkedSinceMs >= PARK_SETTLE_MS && in.nowMs < watch.closeBarUntilMs;
        // An unknown position pauses the attempt for up to 2 s before it stands down.
        const bool pausedUnknown = in.roof == State::Unknown;
        if (seen.after == Phase::WaitPark && watch.attemptOn && !heldBack && !pausedUnknown &&
            in.nowMs - watch.attemptSinceMs > watch.attemptLimitMs)
            return "P6 waited past the park limit without failing the attempt";
        return nullptr;
    }

    // P7: each critical event alerts once per episode.
    const char *checkAlert(const Seen &seen, const Inputs &in, Watch &watch)
    {
        // A closed roof ends the trouble: the throttle starts afresh.
        if (watch.closedByMove)
        {
            watch.closedByMove = false;
            for (auto &row : watch.alertedEver)
                for (bool &ever : row)
                    ever = false;
        }
        if (seen.decision.alert)
        {
            const size_t e = static_cast<size_t>(seen.decision.event);
            const size_t c = static_cast<size_t>(seen.decision.cause);
            if (watch.alertedEver[e][c] && in.nowMs - watch.alertedAtMs[e][c] < REMIND_MS)
                return "P7 a critical event and cause alerted twice within 30 minutes";
            watch.alertedEver[e][c] = true;
            watch.alertedAtMs[e][c] = in.nowMs;
        }
        const bool failed = seen.decision.event == Event::CloseFailed && !seen.decision.reminder;
        if (failed && seen.decision.cause == Cause::TooLong)
            watch.halted = true;
        if (failed && (seen.decision.cause == Cause::Refused || seen.decision.cause == Cause::Interrupted))
        {
            watch.closeBarUntilMs = in.nowMs + RETRY_AFTER_MS;
            watch.closeBarDaylight = seen.decision.daylight;
        }
        return nullptr;
    }

    // P8: a close in motion never outlasts its guard (plus the observer's step).
    const char *checkGuard(const Seen &seen, const Inputs &in, Watch &watch)
    {
        if (seen.after != Phase::Closing || in.roof != State::Closing)
        {
            watch.closingRun = false;
            return nullptr;
        }
        if (!watch.closingRun)
        {
            watch.closingRun = true;
            watch.closingSinceMs = in.nowMs;
        }
        if (in.nowMs - watch.closingSinceMs > in.moveTimeoutMs + CLOSE_GUARD_SLACK_MS + 3100)
            return "P8 a close outlasted its guard";
        return nullptr;
    }

    // P6-P8 and the observer's bookkeeping after a decision.
    const char *checkAfter(const Seen &seen, const Inputs &in, Watch &watch)
    {
        const char *broken = checkAttempt(seen, in, watch);
        if (broken == nullptr)
            broken = checkAlert(seen, in, watch);
        if (broken == nullptr)
            broken = checkGuard(seen, in, watch);
        return broken;
    }

    bool standingDown(Phase phase)
    {
        return phase == Phase::HeldByPerson || phase == Phase::CannotMove || phase == Phase::Halted;
    }

    // P11: unsafe with the roof open never goes quiet for longer than an
    // attempt and its back-off: something is closed, raised or stood down.
    const char *checkLiveness(const Decision &d, const Decision &answer, const Inputs &in, Phase after, Watch &watch)
    {
        const uint64_t stepMs = in.nowMs - watch.lastStepMs;
        const bool prevUnsafe = watch.prevUnsafe;
        watch.prevUnsafe = wanted(in);
        watch.lastStepMs = in.nowMs;
        const bool acted = d.action == Action::Close || isCritical(d.event) || isCritical(answer.event);
        const bool safeLong = watch.safeRun && in.nowMs - watch.safeSinceMs >= REARM_SAFE_MS;
        if (acted || safeLong || in.roof == State::Closed || in.roof == State::Closing || in.closePending || standingDown(after))
        {
            watch.quietUnsafeMs = 0;
            return nullptr;
        }
        // Only time between two unsafe samples counts (a step can straddle a safe spell).
        if (wanted(in) && prevUnsafe)
            watch.quietUnsafeMs += stepMs;
        if (watch.quietUnsafeMs > watch.attemptLimitMs + RETRY_AFTER_MS + 200000)
            return "P11 unsafe and open with no close, alert or stand-down for longer than an attempt and its back-off";
        return nullptr;
    }

    // A random starting world and, sometimes, a park time limit other than the default.
    void startWorld(World &w, Watch &watch, Sequence &seq, Random &rnd)
    {
        if (rnd.chance(300))
        {
            watch.attemptLimitMs = 30000 + rnd.below(240000);
            seq.setParkTimeoutMs(watch.attemptLimitMs);
        }
        w.in.nowMs = rnd.below(100000);
        w.in.weatherSafe = rnd.chance(500);
        w.in.daylight = rnd.chance(400);
        const State starts[] = {State::Open, State::Closed, State::Unknown, State::Stopped, State::Error};
        w.in.roof = starts[rnd.below(5)];
        w.in.parked = rnd.chance(300);
        w.in.ninaConfigured = !rnd.chance(50);
    }

    // A restart of the board (a reset, a crash, a brown-out): only what the
    // sequence keeps survives; a move under way ends between the magnets.
    void restartBoard(World &w, Watch &watch, Sequence &seq, uint32_t &lastStops)
    {
        const Sequence::Saved saved = seq.saved();
        seq = Sequence();
        seq.setParkTimeoutMs(watch.attemptLimitMs);
        seq.restore(saved);
        if (w.in.roof == State::Closing || w.in.roof == State::Opening)
            w.in.roof = State::Unknown;
        w.in.closePending = false;
        w.in.personStops = 0;
        lastStops = 0;
        // What lives only in RAM starts again: the alert throttle and the close back-off.
        for (auto &row : watch.alertedEver)
            for (bool &ever : row)
                ever = false;
        watch.closeBarUntilMs = 0;
        watch.closingRun = false;
        watch.heldPrevUnsafe = false; // the sequence's own sum starts again too
        watch.justRestarted = true;
        watch.weatherSafeRun = false;
        watch.daylightInSafeSpell = false;
        watch.weatherPause = false;
        watch.weatherKept = false;
        // A daylight stand-down or halt keeps the weather's count in the record.
        const bool kept = saved.daylight && (saved.phase == Phase::HeldByPerson || saved.phase == Phase::Halted);
        watch.heldUnsafeMs = kept ? (saved.unsafeMs < WEATHER_REARM_CONFIRM_MS ? saved.unsafeMs : WEATHER_REARM_CONFIRM_MS) : 0;
        // The time the board was down (since the last step) doesn't count: it
        // ran no clock. (On the bench a restart takes a few seconds.)
        const uint64_t downMs = w.in.nowMs - watch.lastStepMs;
        watch.attemptSinceMs += downMs;
        watch.lastStepMs = w.in.nowMs;
    }

    bool heldBack(Phase phase)
    {
        return phase == Phase::HeldByPerson || phase == Phase::Halted || phase == Phase::RetryWait;
    }

    void clearAlerted(Watch &watch)
    {
        for (auto &row : watch.alertedEver)
            for (bool &ever : row)
                ever = false;
    }

    // As if the daylight episode hadn't been: fresh alerts, no daylight close back-off.
    void handToWeather(Watch &watch)
    {
        clearAlerted(watch);
        if (watch.closeBarDaylight)
            watch.closeBarUntilMs = 0;
        watch.anyRequest = false; // the weather's park request may go out at once
    }

    // The weather itself safe for REARM_SAFE_MS without a break.
    bool trackWeatherSafe(Watch &watch, const Inputs &in)
    {
        if (!in.weatherSafe)
            watch.weatherSafeRun = false;
        else if (!watch.weatherSafeRun)
        {
            watch.weatherSafeRun = true;
            watch.weatherSafeSinceMs = in.nowMs;
        }
        return watch.weatherSafeRun && in.nowMs - watch.weatherSafeSinceMs >= REARM_SAFE_MS;
    }

    // A weather episode the weather has left for 10 minutes, with daylight
    // wanting a close in that spell, becomes daylight's.
    bool relabel(Watch &watch, bool weatherSafeLong)
    {
        if (watch.daylightOnly || !weatherSafeLong || !watch.daylightInSafeSpell)
            return false;
        watch.daylightOnly = true;
        watch.heldUnsafeMs = 0;
        return true;
    }

    // After a daylight-only episode the weather's alerts go out afresh.
    void observeIdle(Watch &watch, const Inputs &in)
    {
        if (watch.afterDaylight && !in.weatherSafe)
        {
            handToWeather(watch);
            watch.weatherKept = false;
            watch.afterDaylight = false;
        }
        watch.daylightOnly = in.daylight && in.weatherSafe;
        // Daylight starts its own attempt; the weather's pause waits.
        if (watch.daylightOnly && watch.weatherPause && in.nowMs - watch.weatherPauseAtMs < REARM_SAFE_MS)
        {
            watch.weatherKept = true;
            watch.weatherKeptAtMs = watch.weatherPauseAtMs;
        }
    }

    bool keptValid(const Watch &watch, uint64_t nowMs)
    {
        return watch.weatherKept && nowMs - watch.weatherKeptAtMs < REARM_SAFE_MS;
    }

    // The weather turns safe during its attempt while daylight wants a close:
    // the weather's attempt is kept, daylight starts its own.
    bool pauseForDaylight(Watch &watch, const Inputs &in, Phase before, bool anyStop)
    {
        if (watch.daylightOnly || !in.weatherSafe || !in.daylight || (before != Phase::WaitPark && before != Phase::RetryWait))
            return false;
        watch.weatherKept = true;
        watch.weatherKeptAtMs = in.nowMs;
        watch.daylightOnly = true;
        watch.heldUnsafeMs = 0;
        watch.quietUnsafeMs = 0;
        if (!anyStop)
        {
            watch.attemptOn = true;
            watch.attemptSinceMs = in.nowMs;
        }
        return true;
    }

    // Unsafe weather added up during a person's stand-down: from one step to
    // the next, both not safe; a stop (or another phase) starts it again.
    bool personWaitOver(Watch &watch, const Inputs &in, Phase before, bool anyStop)
    {
        const bool unsafe = !in.weatherSafe;
        if ((before != Phase::HeldByPerson && before != Phase::Halted) || anyStop)
            watch.heldUnsafeMs = 0;
        else if (watch.heldPrevUnsafe)
            watch.heldUnsafeMs += std::min<uint64_t>(in.nowMs - watch.heldPrevMs, MAX_COUNT_STEP_MS);
        watch.heldPrevUnsafe = unsafe;
        watch.heldPrevMs = in.nowMs;
        return watch.heldUnsafeMs >= WEATHER_REARM_CONFIRM_MS && !anyStop;
    }

    // The weather takes a daylight-only episode over.
    void weatherTakesOver(Watch &watch, const Inputs &in, Phase before)
    {
        watch.daylightOnly = false;
        // The weather episode's critical alerts are announced afresh.
        handToWeather(watch);
        watch.resumedWeather = keptValid(watch, in.nowMs) && before != Phase::Closing;
        watch.weatherKept = false;
        if (before == Phase::WaitPark)
        {
            // The weather's own attempt, with its full time limit from now.
            watch.attemptOn = true;
            watch.attemptSinceMs = in.nowMs;
            watch.quietUnsafeMs = 0; // P11 counts from the weather's own attempt
        }
        if (!heldBack(before))
            return;
        watch.expectRearm = true;
        watch.held = false;
        watch.halted = false;
    }

    // The episode as spec 013 defines it: daylight-only from the idle step
    // that starts it with the weather safe, until the weather isn't safe; then
    // nothing from the daylight episode holds the weather's back.
    void observeEpisode(Watch &watch, const Inputs &in, Phase before, bool anyStop)
    {
        watch.expectRearm = false;
        const bool waitOver = personWaitOver(watch, in, before, anyStop);
        const bool restarted = watch.justRestarted;
        watch.justRestarted = false;
        // A close under way at a restart comes back as a back-off (a failed close).
        if (restarted && before == Phase::Closing)
            before = Phase::RetryWait;
        const bool weatherSafeLong = trackWeatherSafe(watch, in);
        watch.daylightInSafeSpell = in.weatherSafe && (watch.daylightInSafeSpell || in.daylight);
        if (before == Phase::Idle)
        {
            observeIdle(watch, in);
            return;
        }
        watch.resumedWeather = false;
        if (relabel(watch, weatherSafeLong) || pauseForDaylight(watch, in, before, anyStop))
            return;
        // A stop on this very step stands it down as daylight's.
        if (in.weatherSafe || !watch.daylightOnly || restarted || anyStop)
            return;
        if ((before == Phase::HeldByPerson || before == Phase::Halted) && !waitOver)
            return;
        weatherTakesOver(watch, in, before);
    }

    // D1 and D2.
    const char *checkEpisode(Watch &watch, const Decision &d, Phase before, Phase after, uint64_t nowMs)
    {
        if (after == Phase::Idle && d.daylight)
            watch.afterDaylight = true;
        // A weather attempt paused by a safe spell (waiting()'s EndedSafe);
        // gone once the sequence leaves Idle or comes back to it another way.
        if (after == Phase::Idle && (before == Phase::WaitPark || before == Phase::RetryWait) && d.event == Event::EndedSafe)
        {
            watch.weatherPause = !d.daylight;
            watch.weatherPauseAtMs = nowMs;
        }
        else if (before != after)
            watch.weatherPause = false;
        // A resumed weather back-off is the weather's own (spec 005 would be there too).
        if (watch.expectRearm && heldBack(after) && !(watch.resumedWeather && after == Phase::RetryWait))
            return "D1 the weather turned unsafe and a daylight-only stand-down or back-off held it back";
        if (d.daylight != watch.daylightOnly)
            return "D2 a decision's episode isn't the observer's";
        return nullptr;
    }

    // One step of the sequence, the roof's answer and every check. nullptr when all hold.
    const char *stepOnce(World &w, Watch &watch, Sequence &seq, Random &rnd, uint32_t &lastStops)
    {
        w.in.nowMs += rnd.chance(20) ? 30000 + rnd.below(120000) : 10 + rnd.below(1500);
        if (rnd.chance(3))
            restartBoard(w, watch, seq, lastStops);
        worldChanges(w, rnd);
        observeInputs(watch, w.in);
        const Phase before = seq.phase();
        // A person's stop seen while the sequence runs stands it down (P4),
        // unless the roof reads closed, which ends it.
        const bool safeLong = watch.safeRun && w.in.nowMs - watch.safeSinceMs >= REARM_SAFE_MS;
        const bool watching = running(before) || (before == Phase::CannotMove && !safeLong);
        const bool anyStop = w.in.personStops != lastStops;
        const bool stopped = anyStop && watching && w.in.roof != State::Closed;
        lastStops = w.in.personStops;
        observeEpisode(watch, w.in, before, anyStop);
        const Decision d = seq.step(w.in);
        const char *episode = checkEpisode(watch, d, before, seq.phase(), w.in.nowMs);
        if (episode != nullptr)
            return episode;
        if (stopped && d.event != Event::StoppedByPerson)
            return "P4 a person's stop didn't stand the sequence down";
        if (stopped)
            watch.held = true;
        const Seen seen{d, before, seq.phase()};
        const char *broken = checkAction(seen, w.in, watch);
        if (broken == nullptr)
            broken = checkAfter(seen, w.in, watch);
        if (broken != nullptr)
            return broken;
        Decision answer;
        carryOut(w, d.action, seq, rnd, answer);
        broken = checkAfter(Seen{answer, Phase::Closing, seq.phase()}, w.in, watch);
        if (broken == nullptr)
            broken = checkLiveness(d, answer, w.in, seq.phase(), watch);
        return broken;
    }

    bool runOne(uint32_t seed, Failure &f)
    {
        Random rnd(seed);
        Sequence seq;
        Watch watch;
        World w;
        uint32_t lastStops = 0;
        startWorld(w, watch, seq, rnd);
        for (int step = 0; step < STEPS_PER_SEQUENCE; ++step)
        {
            const char *broken = stepOnce(w, watch, seq, rnd, lastStops);
            if (broken != nullptr)
                return fail(f, seed, step, broken);
        }
        return true;
    }
} // namespace

void test_random_sequences_keep_every_property()
{
    Failure f{};
    for (uint32_t seed = 1; seed <= SEQUENCES; ++seed)
    {
        if (!runOne(seed, f))
        {
            char message[220];
            snprintf(message, sizeof(message), "seed %u step %d: %s", static_cast<unsigned>(f.seed), f.step, f.what);
            TEST_FAIL_MESSAGE(message);
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_random_sequences_keep_every_property);
    return UNITY_END();
}
