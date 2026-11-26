// Spec 047 (findings F45/F46): the safety poller's schedule. SQMeter's reply
// may take 8 s (a reply of 5 s and more counted as unreachable and closed the
// roof); the weather is polled every 1.5 s while the roof moves (5 s at rest);
// and a slow weather reply never leaves N.I.N.A.'s park reading past its
// freshness (they share one task).

#include <algorithm>
#include <cstdint>
#include <unity.h>

#include "ParkRule.h"
#include "PollSchedule.h"

using namespace SQM::Safety;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t TICK_MS = 100; // SafetyPoller::TICK_MS

    // The poller task (SafetyPoller::run) on a clock: each poll takes the
    // time given. Returns the oldest the park reading got (from the end of one
    // N.I.N.A. read to the end of the next), and counts the polls.
    struct Run
    {
        uint64_t worstNinaAgeMs = 0;
        int ninaReads = 0;
        int weatherPolls = 0;
    };

    template <typename Moving, typename WeatherMs> Run simulateWith(uint64_t ninaMs, WeatherMs weatherMs, uint64_t forMs, Moving moving)
    {
        PollSchedule schedule;
        Run run;
        uint64_t now = 1000;
        uint64_t lastNinaEnd = 0;
        while (now < forMs)
        {
            const PollPass pass = schedule.plan(now, true, true, moving(now));
            if (pass.nina)
            {
                const uint64_t started = now;
                now += ninaMs;
                if (lastNinaEnd != 0)
                    run.worstNinaAgeMs = std::max(run.worstNinaAgeMs, now - lastNinaEnd);
                lastNinaEnd = now;
                schedule.ninaRead(started, now);
                ++run.ninaReads;
            }
            if (pass.sqmeter)
            {
                schedule.sqmeterPolled(now);
                now += weatherMs();
                ++run.weatherPolls;
            }
            now += TICK_MS;
        }
        return run;
    }

    template <typename Moving> Run simulate(uint64_t ninaMs, uint64_t weatherMs, uint64_t forMs, Moving moving)
    {
        return simulateWith(ninaMs, [weatherMs]() { return weatherMs; }, forMs, moving);
    }
} // namespace

void test_the_weather_reply_may_take_eight_seconds(void)
{
    TEST_ASSERT_EQUAL_UINT32(8000, SQMETER_TOTAL_MS);
    TEST_ASSERT_EQUAL_UINT32(8000, SQMETER_LIMITS.totalMs);
    // A 5-6 s reply is waited for (it was cut at 3 s).
    TEST_ASSERT_TRUE(SQMETER_LIMITS.replyMs >= 6000);
    TEST_ASSERT_TRUE(SQMETER_LIMITS.connectMs + SQMETER_LIMITS.replyMs <= SQMETER_LIMITS.totalMs);
    // N.I.N.A.'s are unchanged.
    TEST_ASSERT_EQUAL_UINT32(3000, NINA_LIMITS.connectMs);
    TEST_ASSERT_EQUAL_UINT32(3000, NINA_LIMITS.replyMs);
    TEST_ASSERT_EQUAL_UINT32(6000, NINA_LIMITS.totalMs);
}

void test_the_weather_is_polled_faster_while_the_roof_moves(void)
{
    PollSchedule s;
    TEST_ASSERT_TRUE(s.plan(0, false, true, false).sqmeter); // at once at start
    s.sqmeterPolled(10000);
    TEST_ASSERT_FALSE(s.plan(11499, false, true, true).sqmeter);
    TEST_ASSERT_TRUE(s.plan(11500, false, true, true).sqmeter);
    TEST_ASSERT_FALSE(s.plan(14999, false, true, false).sqmeter);
    TEST_ASSERT_TRUE(s.plan(15000, false, true, false).sqmeter);
    TEST_ASSERT_TRUE(SQMETER_MOVING_POLL_MS >= 1000 && SQMETER_MOVING_POLL_MS <= 2000);
    TEST_ASSERT_EQUAL_UINT32(5000, SQMETER_POLL_MS);
    // The roof starting to move pulls the next poll forward.
    TEST_ASSERT_EQUAL_UINT64(15000, s.sqmeterNextMs(12000, false));
    TEST_ASSERT_EQUAL_UINT64(12000, s.sqmeterNextMs(12000, true));
    // Off: never; changed settings: at once.
    TEST_ASSERT_FALSE(s.plan(99999, false, false, true).sqmeter);
    s.sqmeterChanged();
    TEST_ASSERT_TRUE(s.plan(10001, false, true, false).sqmeter);
    // Over a 60 s move: about every 1.5 s against every 5 s at rest.
    const Run moving = simulate(200, 300, 61000, [](uint64_t) { return true; });
    const Run still = simulate(200, 300, 61000, [](uint64_t) { return false; });
    TEST_ASSERT_TRUE(moving.weatherPolls >= 30);
    TEST_ASSERT_TRUE(still.weatherPolls <= 13);
}

void test_nina_is_read_every_two_seconds_and_before_a_weather_poll(void)
{
    PollSchedule s;
    TEST_ASSERT_TRUE(s.plan(0, true, false, false).nina);
    s.ninaRead(1000, 1200);
    TEST_ASSERT_FALSE(s.plan(2999, true, false, false).nina);
    TEST_ASSERT_TRUE(s.plan(3000, true, false, false).nina);
    s.sqmeterPolled(0);
    // A weather poll due 1 s after the last N.I.N.A. read ended: read it first.
    TEST_ASSERT_TRUE(s.plan(2200, true, true, true).nina);
    TEST_ASSERT_FALSE(s.plan(2199, true, true, true).nina);
    TEST_ASSERT_FALSE(s.plan(99999, false, true, true).nina); // no address
    s.ninaChanged();
    TEST_ASSERT_TRUE(s.plan(1300, true, false, false).nina);
}

// The finding: SQMeter taking as long as it may on every poll, the roof moving
// (a weather poll due every pass), and at rest. Before, N.I.N.A. was read only
// every 2 s whatever the weather poll took.
void test_a_slow_weather_reply_never_starves_the_park_reading(void)
{
    for (uint64_t ninaMs : {50u, 300u, 800u})
    {
        for (int movingNow = 0; movingNow < 2; ++movingNow)
        {
            const Run run = simulate(ninaMs, SQMETER_TOTAL_MS, 300000, [movingNow](uint64_t) { return movingNow == 1; });
            TEST_ASSERT_TRUE(run.worstNinaAgeMs <= NINA_FRESH_MS);
            TEST_ASSERT_TRUE(run.weatherPolls > 0);
        }
    }
    // The roof starting and stopping on its own schedule.
    const Run mixed = simulate(300, SQMETER_TOTAL_MS, 300000, [](uint64_t t) { return (t / 37000) % 2 == 0; });
    TEST_ASSERT_TRUE(mixed.worstNinaAgeMs <= NINA_FRESH_MS);
    // A SQMeter whose replies vary between quick and as long as they may: a
    // long one can come due just before N.I.N.A. is (the case read first fixes).
    for (uint64_t ninaMs : {50u, 300u, 800u})
    {
        for (int movingNow = 0; movingNow < 2; ++movingNow)
        {
            uint32_t seed = 12345;
            const Run varied = simulateWith(
                ninaMs,
                [&seed]()
                {
                    seed = seed * 1103515245u + 12345u;
                    const uint64_t choices[] = {100, 400, 1300, 1700, 2500, 5200, SQMETER_TOTAL_MS};
                    return choices[(seed >> 16) % 7];
                },
                600000,
                [movingNow](uint64_t) { return movingNow == 1; });
            TEST_ASSERT_TRUE(varied.worstNinaAgeMs <= NINA_FRESH_MS);
        }
    }
    // With a quick SQMeter, N.I.N.A. stays at about every 2 s.
    const Run quick = simulate(100, 200, 60000, [](uint64_t) { return false; });
    TEST_ASSERT_TRUE(quick.worstNinaAgeMs <= NINA_POLL_MS + 2 * TICK_MS + 200);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_weather_reply_may_take_eight_seconds);
    RUN_TEST(test_the_weather_is_polled_faster_while_the_roof_moves);
    RUN_TEST(test_nina_is_read_every_two_seconds_and_before_a_weather_poll);
    RUN_TEST(test_a_slow_weather_reply_never_starves_the_park_reading);
    return UNITY_END();
}
