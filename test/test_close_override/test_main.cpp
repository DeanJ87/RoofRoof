// Spec 027 (constitution 7.0.0, III): a person's Open override keeps its own
// reasons from starting the automatic close, and nothing else.

#include <unity.h>

#include "CloseSequence.h"

using namespace SQM;
using namespace SQM::CloseSeq;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Safety::WeatherVerdict unsafe(uint32_t reasons)
    {
        Safety::WeatherVerdict v;
        v.verdict = Safety::Verdict::Unsafe;
        v.reasons = reasons;
        return v;
    }

    Inputs wanting(bool daylight)
    {
        Inputs in;
        in.weatherSafe = false;
        in.daylight = daylight;
        return in;
    }
} // namespace

void test_weather_override_covers_only_the_sources_own_unsafe(void)
{
    Inputs in = wanting(false);
    applyOverride(in, RoofIo::OverrideWeather, unsafe(Safety::ReasonWeatherUnsafe));
    TEST_ASSERT_TRUE(in.weatherSafe);
    const uint32_t hardReasons[] = {
        Safety::ReasonRain,
        Safety::ReasonRainSensorFault,
        Safety::ReasonSourceStale,
        Safety::ReasonSourceUnreachable,
        Safety::ReasonNoWeatherSource,
        Safety::ReasonWeatherUnsafe | Safety::ReasonRain};
    for (const uint32_t hard : hardReasons)
    {
        Inputs again = wanting(false);
        applyOverride(again, RoofIo::OverrideWeather, unsafe(hard));
        TEST_ASSERT_FALSE(again.weatherSafe);
    }
}

void test_daylight_override_and_no_override(void)
{
    Inputs in = wanting(true);
    applyOverride(in, RoofIo::OverrideDaylight, unsafe(Safety::ReasonWeatherUnsafe));
    TEST_ASSERT_FALSE(in.daylight);
    TEST_ASSERT_FALSE(in.weatherSafe); // the weather wasn't overridden
    Inputs none = wanting(true);
    applyOverride(none, 0, unsafe(Safety::ReasonWeatherUnsafe));
    TEST_ASSERT_TRUE(none.daylight);
    TEST_ASSERT_FALSE(none.weatherSafe);
    Inputs park = wanting(true);
    applyOverride(park, RoofIo::OverridePark, unsafe(Safety::ReasonWeatherUnsafe));
    TEST_ASSERT_TRUE(park.daylight);
}

// An overridden open in cloud: the sequence stays idle; rain starts it.
void test_the_sequence_stays_idle_until_rain(void)
{
    Sequence seq;
    Inputs in;
    in.roof = Roof::State::Open;
    in.parked = true;
    in.weatherSafe = false;
    for (uint64_t t = 0; t < 60000; t += 100)
    {
        Inputs step = in;
        step.nowMs = t;
        applyOverride(step, RoofIo::OverrideWeather, unsafe(Safety::ReasonWeatherUnsafe));
        TEST_ASSERT_EQUAL(Action::None, seq.step(step).action);
        TEST_ASSERT_EQUAL(Phase::Idle, seq.phase());
    }
    Inputs rain = in;
    rain.nowMs = 60000;
    applyOverride(rain, RoofIo::OverrideWeather, unsafe(Safety::ReasonWeatherUnsafe | Safety::ReasonRain));
    seq.step(rain);
    TEST_ASSERT_TRUE(seq.phase() != Phase::Idle);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_weather_override_covers_only_the_sources_own_unsafe);
    RUN_TEST(test_daylight_override_and_no_override);
    RUN_TEST(test_the_sequence_stays_idle_until_rain);
    return UNITY_END();
}
