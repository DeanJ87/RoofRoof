// The motion logic's building blocks (spec 002, lib/RoofLogic): settings and
// their ranges, the Hall filter (FR-020) and the speed profile with its expected
// times and timeouts (FR-009, FR-010, FR-014).

#include <cmath>
#include <unity.h>

#include "Config.h"
#include "HallFilter.h"
#include "MotionProfile.h"
#include "RoofSettings.h"
#include "RoofTypes.h"

using namespace SQM;
using namespace SQM::Roof;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const HallThresholds THRESHOLDS{3000, 1100};

    HallLevel feed(HallFilter &filter, uint16_t counts, int times)
    {
        HallLevel level = filter.level();
        for (int i = 0; i < times; ++i)
            level = filter.update(counts, THRESHOLDS);
        return level;
    }

    // The profile integrated step by step, as the controller drives it.
    uint32_t simulatedMoveMs(const Settings &s, float distanceMm, uint32_t stepMs)
    {
        float covered = 0.0f;
        uint32_t elapsed = 0;
        while (covered < distanceMm && elapsed < 10000000)
        {
            const uint8_t duty = toDuty(profileFraction(s, elapsed, distanceMm - covered));
            covered += advanceMm(s, duty, stepMs);
            elapsed += stepMs;
        }
        return elapsed;
    }
} // namespace

// --- Names and settings -----------------------------------------------------

void test_machine_names_are_stable(void)
{
    TEST_ASSERT_EQUAL_STRING("stopped", stateName(State::Stopped));
    TEST_ASSERT_EQUAL_STRING("error", stateName(State::Error));
    TEST_ASSERT_EQUAL_STRING("open", endName(End::Open));
    TEST_ASSERT_EQUAL_STRING("wrongMagnet", faultName(FaultKind::WrongMagnet));
    TEST_ASSERT_EQUAL_STRING("external", faultName(FaultKind::External));
    TEST_ASSERT_EQUAL_STRING("positionUnknown", refusalName(Refusal::PositionUnknown));
    TEST_ASSERT_EQUAL_STRING("moving", refusalName(Refusal::Moving));
    TEST_ASSERT_EQUAL_STRING("rail", hallLevelName(HallLevel::Rail));
    TEST_ASSERT_EQUAL_STRING("unknown", stateName(static_cast<State>(99)));
    TEST_ASSERT_EQUAL_STRING("unknown", faultName(static_cast<FaultKind>(99)));
}

void test_default_settings_are_valid_and_match_the_stored_defaults(void)
{
    const Settings defaults;
    TEST_ASSERT_TRUE(validSettings(defaults));
    const Settings mapped = settingsFromConfig(RoofConfig());
    TEST_ASSERT_TRUE(validSettings(mapped));
    TEST_ASSERT_EQUAL_UINT32(defaults.travelMm, mapped.travelMm);
    TEST_ASSERT_EQUAL_FLOAT(defaults.speedMmPerSecond, mapped.speedMmPerSecond);
    TEST_ASSERT_EQUAL_UINT32(defaults.timeoutMarginMs, mapped.timeoutMarginMs);
    TEST_ASSERT_EQUAL_UINT32(defaults.leaveMagnetMs, mapped.leaveMagnetMs);
    TEST_ASSERT_EQUAL_UINT32(defaults.rampUpMs, mapped.rampUpMs);
    TEST_ASSERT_EQUAL_UINT32(defaults.rampDownMs, mapped.rampDownMs);
    TEST_ASSERT_EQUAL_UINT8(defaults.topDutyPercent, mapped.topDutyPercent);
    TEST_ASSERT_EQUAL_UINT8(defaults.slowDutyPercent, mapped.slowDutyPercent);
    TEST_ASSERT_TRUE(defaults.openDirection == mapped.openDirection);
    TEST_ASSERT_TRUE(defaults.hallHighEnd == mapped.hallHighEnd);
    TEST_ASSERT_EQUAL_UINT16(defaults.hallHighCounts, mapped.hallHighCounts);
    TEST_ASSERT_EQUAL_UINT16(defaults.hallLowCounts, mapped.hallLowCounts);
    TEST_ASSERT_FALSE(mapped.currentSenseEnabled);
    TEST_ASSERT_EQUAL_FLOAT(defaults.maxCurrentAmps, mapped.maxCurrentAmps);
    TEST_ASSERT_EQUAL_FLOAT(defaults.minSupplyVolts, mapped.minSupplyVolts);
}

void test_config_enums_and_units_map_to_the_logic(void)
{
    RoofConfig config;
    config.openDirection = RoofConfig::Direction::Reverse;
    config.hallHighEnd = RoofConfig::HallEnd::Closed;
    config.timeoutMarginSeconds = 20;
    config.leaveMagnetSeconds = 4;
    const Settings s = settingsFromConfig(config);
    TEST_ASSERT_TRUE(s.openDirection == Driver::Command::Reverse);
    TEST_ASSERT_TRUE(s.hallHighEnd == End::Closed);
    TEST_ASSERT_EQUAL_UINT32(20000, s.timeoutMarginMs);
    TEST_ASSERT_EQUAL_UINT32(4000, s.leaveMagnetMs);
}

// A stored value too big for the logic's field becomes invalid, never wraps into range.
void test_out_of_range_config_is_invalid_not_wrapped(void)
{
    RoofConfig config;
    config.slowDutyPercent = 256 + 40;
    TEST_ASSERT_FALSE(validSettings(settingsFromConfig(config)));
    config = RoofConfig();
    config.hallHighCounts = 65536 + 3000;
    TEST_ASSERT_FALSE(validSettings(settingsFromConfig(config)));
    config = RoofConfig();
    config.timeoutMarginSeconds = 5000000; // would overflow in ms
    TEST_ASSERT_FALSE(validSettings(settingsFromConfig(config)));
}

void test_valid_settings_bounds(void)
{
    Settings s;
    s.slowDutyPercent = 60;
    s.topDutyPercent = 59;
    TEST_ASSERT_FALSE(validSettings(s)); // slow above top
    s.topDutyPercent = 60;
    TEST_ASSERT_TRUE(validSettings(s)); // equal is fine
    s = Settings();
    s.hallLowCounts = 2920;
    TEST_ASSERT_TRUE(validSettings(s)); // exactly 80 apart
    s.hallLowCounts = 2921;
    TEST_ASSERT_FALSE(validSettings(s));
    s = Settings();
    s.speedMmPerSecond = NAN;
    TEST_ASSERT_FALSE(validSettings(s));
    s = Settings();
    s.speedMmPerSecond = 0.0f;
    TEST_ASSERT_FALSE(validSettings(s));
    s = Settings();
    s.openDirection = Driver::Command::Off;
    TEST_ASSERT_FALSE(validSettings(s));
    s = Settings();
    s.travelMm = 99;
    TEST_ASSERT_FALSE(validSettings(s));
    s.travelMm = 20000;
    TEST_ASSERT_TRUE(validSettings(s));
    s.rampUpMs = 30001;
    TEST_ASSERT_FALSE(validSettings(s));
    s = Settings();
    s.minSupplyVolts = 5.9f;
    TEST_ASSERT_FALSE(validSettings(s));
    s = Settings();
    s.maxCurrentAmps = 40.1f;
    TEST_ASSERT_FALSE(validSettings(s));
}

// --- Hall filter (FR-020) ---------------------------------------------------

void test_hall_unsettled_until_three_equal_readings(void)
{
    HallFilter filter;
    TEST_ASSERT_TRUE(filter.level() == HallLevel::Unsettled);
    TEST_ASSERT_TRUE(feed(filter, 2000, 2) == HallLevel::Unsettled);
    TEST_ASSERT_TRUE(feed(filter, 2000, 1) == HallLevel::Between);
}

void test_hall_thresholds_are_inclusive(void)
{
    TEST_ASSERT_TRUE(HallFilter::classify(3000, THRESHOLDS, HallLevel::Between) == HallLevel::High);
    TEST_ASSERT_TRUE(HallFilter::classify(2999, THRESHOLDS, HallLevel::Between) == HallLevel::Between);
    TEST_ASSERT_TRUE(HallFilter::classify(1100, THRESHOLDS, HallLevel::Between) == HallLevel::Low);
    TEST_ASSERT_TRUE(HallFilter::classify(1101, THRESHOLDS, HallLevel::Between) == HallLevel::Between);
}

void test_hall_leaving_a_magnet_needs_the_hysteresis_band(void)
{
    TEST_ASSERT_TRUE(HallFilter::classify(2961, THRESHOLDS, HallLevel::High) == HallLevel::High);
    TEST_ASSERT_TRUE(HallFilter::classify(2960, THRESHOLDS, HallLevel::High) == HallLevel::High);
    TEST_ASSERT_TRUE(HallFilter::classify(2959, THRESHOLDS, HallLevel::High) == HallLevel::Between);
    TEST_ASSERT_TRUE(HallFilter::classify(1140, THRESHOLDS, HallLevel::Low) == HallLevel::Low);
    TEST_ASSERT_TRUE(HallFilter::classify(1141, THRESHOLDS, HallLevel::Low) == HallLevel::Between);
    // Hysteresis only applies to the magnet already confirmed.
    TEST_ASSERT_TRUE(HallFilter::classify(2970, THRESHOLDS, HallLevel::Low) == HallLevel::Between);
}

void test_hall_rails(void)
{
    TEST_ASSERT_TRUE(HallFilter::classify(30, THRESHOLDS, HallLevel::Low) == HallLevel::Rail);
    TEST_ASSERT_TRUE(HallFilter::classify(31, THRESHOLDS, HallLevel::Low) == HallLevel::Low);
    TEST_ASSERT_TRUE(HallFilter::classify(4065, THRESHOLDS, HallLevel::High) == HallLevel::Rail);
    TEST_ASSERT_TRUE(HallFilter::classify(4064, THRESHOLDS, HallLevel::High) == HallLevel::High);
    TEST_ASSERT_TRUE(HallFilter::classify(0, THRESHOLDS, HallLevel::Between) == HallLevel::Rail);
    TEST_ASSERT_TRUE(HallFilter::classify(4095, THRESHOLDS, HallLevel::Between) == HallLevel::Rail);
}

void test_hall_a_changed_candidate_restarts_the_count(void)
{
    HallFilter filter;
    feed(filter, 2000, 3);
    feed(filter, 3200, 2);
    TEST_ASSERT_TRUE(feed(filter, 900, 2) == HallLevel::Between); // two high, then two low: neither confirmed
    TEST_ASSERT_TRUE(feed(filter, 900, 1) == HallLevel::Low);
    // A single reading back at the confirmed level resets the count too.
    feed(filter, 2000, 2);
    feed(filter, 900, 1);
    TEST_ASSERT_TRUE(feed(filter, 2000, 2) == HallLevel::Low);
    TEST_ASSERT_TRUE(feed(filter, 2000, 1) == HallLevel::Between);
}

void test_hall_rail_readings_counted_in_a_window(void)
{
    HallFilter filter;
    feed(filter, 2000, 3);
    // Alternating good and rail: the third rail reading within eight confirms it.
    TEST_ASSERT_TRUE(feed(filter, 4095, 1) == HallLevel::Between);
    TEST_ASSERT_TRUE(feed(filter, 2000, 1) == HallLevel::Between);
    TEST_ASSERT_TRUE(feed(filter, 4095, 1) == HallLevel::Between);
    TEST_ASSERT_TRUE(feed(filter, 2000, 1) == HallLevel::Between);
    TEST_ASSERT_TRUE(feed(filter, 0, 1) == HallLevel::Rail);
    // It stays a rail until the rail readings have left the window and a level is confirmed.
    TEST_ASSERT_TRUE(feed(filter, 2000, 5) == HallLevel::Rail);
    TEST_ASSERT_TRUE(feed(filter, 2000, 1) == HallLevel::Between);
}

void test_isolated_rail_readings_do_not_interrupt_a_magnet(void)
{
    HallFilter filter;
    feed(filter, 2000, 3);
    feed(filter, 3200, 2);
    feed(filter, 4095, 1); // a spike between two magnet readings
    TEST_ASSERT_TRUE(feed(filter, 3200, 1) == HallLevel::High);
}

void test_reading_sanity_bounds(void)
{
    TEST_ASSERT_TRUE(supplyReadable(16.0f));
    TEST_ASSERT_FALSE(supplyReadable(16.001f));
    TEST_ASSERT_FALSE(supplyReadable(INFINITY));
    TEST_ASSERT_FALSE(supplyReadable(NAN));
    TEST_ASSERT_TRUE(supplyReadable(0.0f)); // readable, and low
    TEST_ASSERT_TRUE(currentReadable(-0.5f));
    TEST_ASSERT_FALSE(currentReadable(-0.51f));
    TEST_ASSERT_FALSE(currentReadable(INFINITY));
    TEST_ASSERT_FALSE(currentReadable(NAN));
}

void test_drive_budget_is_the_longest_single_move(void)
{
    const Settings s;
    TEST_ASSERT_EQUAL_UINT32(unknownMoveTimeoutMs(s), driveBudgetMs(s));
    TEST_ASSERT_TRUE(driveBudgetMs(s) >= moveTimeoutMs(s, static_cast<float>(s.travelMm)));
}

// --- Profile (FR-009, FR-010) -----------------------------------------------

void test_to_duty_rounds_and_clamps(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, toDuty(-1.0f));
    TEST_ASSERT_EQUAL_UINT8(0, toDuty(NAN));
    TEST_ASSERT_EQUAL_UINT8(102, toDuty(0.4f));
    TEST_ASSERT_EQUAL_UINT8(255, toDuty(1.0f));
    TEST_ASSERT_EQUAL_UINT8(255, toDuty(7.0f));
    for (int a = 0; a <= 100; ++a)
        for (int b = a; b <= 100; ++b)
            TEST_ASSERT_TRUE(toDuty(a / 100.0f) <= toDuty(b / 100.0f));
}

void test_ramp_up_is_linear_from_slow_to_top(void)
{
    const Settings s; // slow 40%, top 100%, ramp-up 2000 ms
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.4f, profileFraction(s, 0, 1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.7f, profileFraction(s, 1000, 1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, profileFraction(s, 2000, 1000.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, profileFraction(s, 60000, 1000.0f));
}

void test_slow_down_by_estimated_distance(void)
{
    const Settings s; // zone = 38 mm/s x 1.0 x 3 s = 114 mm
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, profileFraction(s, 10000, 114.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.7f, profileFraction(s, 10000, 57.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.4f, profileFraction(s, 10000, 0.001f));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.4f, profileFraction(s, 10000, 0.0f));  // arrived: slow
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.4f, profileFraction(s, 10000, -5.0f)); // overshot estimate: slow
}

void test_profile_never_outside_slow_and_top(void)
{
    Settings s;
    s.topDutyPercent = 70;
    s.slowDutyPercent = 25;
    for (uint32_t t = 0; t <= 5000; t += 37)
        for (float r = -10.0f; r <= 2000.0f; r += 13.7f)
        {
            const float f = profileFraction(s, t, r);
            TEST_ASSERT_TRUE(f >= 0.25f - 1e-6f && f <= 0.70f + 1e-6f);
            TEST_ASSERT_TRUE(toDuty(f) <= toDuty(0.70f));
        }
}

void test_no_ramps_is_top_duty_throughout(void)
{
    Settings s;
    s.rampUpMs = 0;
    s.rampDownMs = 0;
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, profileFraction(s, 0, 1.0f));
    TEST_ASSERT_EQUAL_UINT32(1850u * 1000u / 38u + 1, expectedMoveMs(s, 1850.0f));
}

void test_advance_is_speed_times_duty(void)
{
    const Settings s;
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 38.0f, advanceMm(s, 255, 1000));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, advanceMm(s, 0, 1000));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 38.0f * 102.0f / 255.0f * 0.02f, advanceMm(s, 102, 20));
}

// The closed form agrees with driving the profile step by step (within 1%).
void test_expected_time_matches_the_driven_profile(void)
{
    Settings variants[4];
    variants[1].topDutyPercent = 60;
    variants[1].slowDutyPercent = 60;
    variants[2].rampUpMs = 8000;
    variants[2].rampDownMs = 10000;
    variants[3].slowDutyPercent = 10;
    variants[3].rampUpMs = 0;
    for (const Settings &s : variants)
        for (float distance : {10.0f, 100.0f, 500.0f, 1850.0f})
        {
            const uint32_t expected = expectedMoveMs(s, distance);
            const uint32_t driven = simulatedMoveMs(s, distance, 5);
            TEST_ASSERT_UINT32_WITHIN(driven / 100 + 20, driven, expected);
        }
}

void test_expected_time_for_the_default_roof(void)
{
    // 1850 mm at 38 mm/s is 48.7 s at full speed; the ramps add a few seconds.
    const uint32_t ms = expectedMoveMs(Settings(), 1850.0f);
    TEST_ASSERT_TRUE(ms > 48700 && ms < 53000);
    TEST_ASSERT_EQUAL_UINT32(0, expectedMoveMs(Settings(), 0.0f));
    TEST_ASSERT_EQUAL_UINT32(0, expectedMoveMs(Settings(), -3.0f));
}

void test_timeouts_add_the_margin(void)
{
    const Settings s;
    TEST_ASSERT_EQUAL_UINT32(expectedMoveMs(s, 1850.0f) + 15000, moveTimeoutMs(s, 1850.0f));
    TEST_ASSERT_EQUAL_UINT32(15000, moveTimeoutMs(s, 0.0f));
    // Unknown position: the whole travel at the slow duty (1850 / (38 x 0.4) = 121.7 s) + margin.
    TEST_ASSERT_UINT32_WITHIN(2, 121711 + 15000, unknownMoveTimeoutMs(s));
}

// The slowest, longest valid roof still fits comfortably in 32 bits.
void test_worst_valid_settings_do_not_overflow(void)
{
    Settings s;
    s.travelMm = RoofConfig::MAX_TRAVEL_MM;
    s.speedMmPerSecond = RoofConfig::MIN_SPEED_MM_PER_SECOND;
    s.topDutyPercent = RoofConfig::MIN_TOP_DUTY_PERCENT;
    s.slowDutyPercent = RoofConfig::MIN_SLOW_DUTY_PERCENT;
    s.rampUpMs = RoofConfig::MAX_RAMP_MS;
    s.rampDownMs = RoofConfig::MAX_RAMP_MS;
    s.timeoutMarginMs = RoofConfig::MAX_TIMEOUT_MARGIN_SECONDS * 1000;
    TEST_ASSERT_TRUE(validSettings(s));
    const uint32_t known = moveTimeoutMs(s, static_cast<float>(s.travelMm));
    const uint32_t unknown = unknownMoveTimeoutMs(s);
    TEST_ASSERT_TRUE(known > 100000000u && known < 0x7FFFFFFFu);
    TEST_ASSERT_TRUE(unknown > 200000000u && unknown < 0x7FFFFFFFu);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_machine_names_are_stable);
    RUN_TEST(test_default_settings_are_valid_and_match_the_stored_defaults);
    RUN_TEST(test_config_enums_and_units_map_to_the_logic);
    RUN_TEST(test_out_of_range_config_is_invalid_not_wrapped);
    RUN_TEST(test_valid_settings_bounds);
    RUN_TEST(test_hall_unsettled_until_three_equal_readings);
    RUN_TEST(test_hall_thresholds_are_inclusive);
    RUN_TEST(test_hall_leaving_a_magnet_needs_the_hysteresis_band);
    RUN_TEST(test_hall_rails);
    RUN_TEST(test_hall_a_changed_candidate_restarts_the_count);
    RUN_TEST(test_hall_rail_readings_counted_in_a_window);
    RUN_TEST(test_isolated_rail_readings_do_not_interrupt_a_magnet);
    RUN_TEST(test_reading_sanity_bounds);
    RUN_TEST(test_drive_budget_is_the_longest_single_move);
    RUN_TEST(test_to_duty_rounds_and_clamps);
    RUN_TEST(test_ramp_up_is_linear_from_slow_to_top);
    RUN_TEST(test_slow_down_by_estimated_distance);
    RUN_TEST(test_profile_never_outside_slow_and_top);
    RUN_TEST(test_no_ramps_is_top_duty_throughout);
    RUN_TEST(test_advance_is_speed_times_duty);
    RUN_TEST(test_expected_time_matches_the_driven_profile);
    RUN_TEST(test_expected_time_for_the_default_roof);
    RUN_TEST(test_timeouts_add_the_margin);
    RUN_TEST(test_worst_valid_settings_do_not_overflow);
    return UNITY_END();
}
