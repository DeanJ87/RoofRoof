// Spec 027 (constitution 7.0.0): the refusals a person may confirm past, the
// override in effect and its end, and the park interlock during an open.

#include <unity.h>

#include "RoofGates.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    GateInputs parkedWith(uint8_t fitted)
    {
        GateInputs in;
        in.weather = Safety::Verdict::Safe;
        in.parked = true;
        in.fittedSwitches = fitted;
        in.switch1 = Contact::Closed;
        in.switch2 = Contact::Closed;
        return in;
    }

    Roof::Status inState(Roof::State state)
    {
        Roof::Status s;
        s.state = state;
        return s;
    }
} // namespace

// The park can't be overridden for an Open by any bit but the internal park one.
void test_open_park_refusal_is_not_overridden_by_weather_or_daylight(void)
{
    GateInputs in = parkedWith(0);
    in.parked = false;
    TEST_ASSERT_EQUAL(GateRefusal::MountNotParked, gateCommand(Roof::Command::Open, in, OverrideWeather | OverrideDaylight));
}

void test_only_the_sources_own_unsafe_is_overridable(void)
{
    GateInputs in = parkedWith(0);
    in.weather = Safety::Verdict::Unsafe;
    in.weatherReasons = Safety::ReasonWeatherUnsafe;
    TEST_ASSERT_TRUE(weatherOverridable(in));
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Open, in, OverrideWeather));
    TEST_ASSERT_EQUAL(GateRefusal::WeatherNotSafe, gateCommand(Roof::Command::Open, in, OverrideDaylight));
    const uint32_t hard[] = {
        Safety::ReasonRain,
        Safety::ReasonRainSensorFault,
        Safety::ReasonSourceStale,
        Safety::ReasonSourceUnreachable,
        Safety::ReasonSourceUnreadable,
        Safety::ReasonSourceOffline,
        Safety::ReasonNoWeatherSource,
        Safety::ReasonWeatherUnsafe | Safety::ReasonRain,
        Safety::ReasonWeatherUnsafe | Safety::ReasonSourceStale};
    for (const uint32_t reasons : hard)
    {
        in.weatherReasons = reasons;
        TEST_ASSERT_FALSE(weatherOverridable(in));
        TEST_ASSERT_EQUAL(GateRefusal::WeatherNotSafe, gateCommand(Roof::Command::Open, in, OverrideWeather));
    }
    in.weather = Safety::Verdict::Unknown;
    in.weatherReasons = Safety::ReasonWeatherUnsafe;
    TEST_ASSERT_FALSE(weatherOverridable(in));
}

void test_daylight_is_overridable_for_open(void)
{
    GateInputs in = parkedWith(0);
    in.daylightCloses = true;
    TEST_ASSERT_EQUAL(GateRefusal::Daylight, gateCommand(Roof::Command::Open, in));
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Open, in, OverrideDaylight));
    TEST_ASSERT_EQUAL_UINT8(OverrideDaylight, overridableNow(Roof::Command::Open, in));
    in.weather = Safety::Verdict::Unsafe;
    in.weatherReasons = Safety::ReasonWeatherUnsafe;
    TEST_ASSERT_EQUAL_UINT8(OverrideDaylight | OverrideWeather, overridableNow(Roof::Command::Open, in));
}

void test_close_park_override(void)
{
    GateInputs in = parkedWith(2);
    in.parked = false;
    in.switch2 = Contact::Open;
    TEST_ASSERT_EQUAL_UINT8(OverridePark, overridableNow(Roof::Command::Close, in));
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Close, in, OverridePark));
    TEST_ASSERT_EQUAL_UINT8(0, overridableNow(Roof::Command::Close, parkedWith(0)));
}

void test_override_names(void)
{
    TEST_ASSERT_EQUAL_UINT8(OverridePark, overrideBit("mountNotParked"));
    TEST_ASSERT_EQUAL_UINT8(OverrideWeather, overrideBit("weatherNotSafe"));
    TEST_ASSERT_EQUAL_UINT8(OverrideDaylight, overrideBit("daylight"));
    TEST_ASSERT_EQUAL_UINT8(0, overrideBit("fault"));
    TEST_ASSERT_EQUAL_UINT8(0, overrideBit(nullptr));
}

// An override in effect lasts while its reasons do and ends at the first
// reason it doesn't cover.
void test_override_in_effect_ends_with_its_reasons_or_a_new_one(void)
{
    GateInputs in = parkedWith(0);
    in.weather = Safety::Verdict::Unsafe;
    in.weatherReasons = Safety::ReasonWeatherUnsafe;
    in.daylightCloses = true;
    const uint8_t both = OverrideWeather | OverrideDaylight;
    TEST_ASSERT_EQUAL_UINT8(both, overrideStillApplies(both, in));
    in.daylightCloses = false; // night falls: the weather part stays
    TEST_ASSERT_EQUAL_UINT8(OverrideWeather, overrideStillApplies(both, in));
    in.weatherReasons = Safety::ReasonWeatherUnsafe | Safety::ReasonRain; // rain: ends
    TEST_ASSERT_EQUAL_UINT8(0, overrideStillApplies(both, in));
    in.weather = Safety::Verdict::Safe; // everything clear: ends
    in.weatherReasons = 0;
    TEST_ASSERT_EQUAL_UINT8(0, overrideStillApplies(both, in));
    in.daylightCloses = true; // daylight, but only the weather was overridden
    TEST_ASSERT_EQUAL_UINT8(0, overrideStillApplies(OverrideWeather, in));
}

void test_an_override_keeps_an_open_going_for_its_reasons_only(void)
{
    GateInputs in = parkedWith(0);
    in.daylightCloses = true;
    const Roof::Status opening = inState(Roof::State::Opening);
    TEST_ASSERT_TRUE(openMustStop(opening, in));
    TEST_ASSERT_FALSE(openMustStop(opening, in, OverrideDaylight));
    in.weather = Safety::Verdict::Unsafe;
    in.weatherReasons = Safety::ReasonRain;
    TEST_ASSERT_TRUE(openMustStop(opening, in, OverrideDaylight | OverrideWeather));
    Roof::Status jog = inState(Roof::State::Stopped);
    jog.jogging = true;
    jog.target = Roof::End::Open;
    in.weather = Safety::Verdict::Safe;
    TEST_ASSERT_TRUE(openMustStop(jog, in, OverrideDaylight)); // a jog carries no override
}

void test_the_interlock_watches_an_open_and_not_an_overridden_close(void)
{
    GateInputs in = parkedWith(0);
    in.parked = false;
    TEST_ASSERT_TRUE(interlockLost(inState(Roof::State::Opening), in));
    TEST_ASSERT_TRUE(interlockLost(inState(Roof::State::Closing), in));
    TEST_ASSERT_FALSE(interlockLost(inState(Roof::State::Closing), in, true));
    TEST_ASSERT_TRUE(interlockLost(inState(Roof::State::Opening), in, true));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_park_refusal_is_not_overridden_by_weather_or_daylight);
    RUN_TEST(test_only_the_sources_own_unsafe_is_overridable);
    RUN_TEST(test_daylight_is_overridable_for_open);
    RUN_TEST(test_close_park_override);
    RUN_TEST(test_override_names);
    RUN_TEST(test_override_in_effect_ends_with_its_reasons_or_a_new_one);
    RUN_TEST(test_an_override_keeps_an_open_going_for_its_reasons_only);
    RUN_TEST(test_the_interlock_watches_an_open_and_not_an_overridden_close);
    return UNITY_END();
}
