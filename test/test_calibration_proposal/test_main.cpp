// Spec 016: the calibration run's statistics and its proposal
// (lib/CalibrationLogic, research R5-R7), from hand-made measurements.

#include <cmath>
#include <unity.h>

#include "CalibrationProposal.h"
#include "CalibrationStats.h"
#include "MotionProfile.h"

using namespace SQM;
using namespace SQM::Calib;
using Roof::End;
using Roof::HallLevel;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Stats constant(uint16_t value, uint32_t count = 50)
    {
        Stats s;
        for (uint32_t i = 0; i < count; ++i)
            s.add(value);
        return s;
    }

    // Readings value ± spread, alternating.
    Stats spread(uint16_t value, uint16_t delta, uint32_t count = 100)
    {
        Stats s;
        for (uint32_t i = 0; i < count; ++i)
            s.add(static_cast<uint16_t>(i % 2 ? value + delta : value - delta));
        return s;
    }

    MeasuredLeg legAt(LegKind kind, uint8_t duty, float speed100, uint32_t travelMm)
    {
        MeasuredLeg leg;
        leg.kind = kind;
        leg.target = kind == LegKind::SlowOpen || kind == LegKind::TopOpen ? End::Open : End::Closed;
        leg.dutyPercent = duty;
        const float seconds = travelMm / (speed100 * duty / 100.0f);
        leg.drivenMs = static_cast<uint32_t>(seconds * 1000.0f);
        leg.dutyMs = leg.drivenMs * duty / 100.0f;
        return leg;
    }

    // A good run at the default roof: 38 mm/s, the open end high.
    Measurements good(float openSpeed = 38.0f, float closeSpeed = 38.0f)
    {
        Measurements m;
        m.travelMm = 1850;
        m.legs[0] = legAt(LegKind::SlowOpen, 40, openSpeed, 1850);
        m.legs[1] = legAt(LegKind::SlowClose, 40, closeSpeed, 1850);
        m.legCount = 2;
        m.between = constant(2050);
        m.closedRest = constant(700);
        m.openRest = constant(3400);
        m.closedLevel = HallLevel::Low;
        m.openLevel = HallLevel::High;
        return m;
    }
} // namespace

// --- Statistics --------------------------------------------------------------------

void test_stats_mean_sigma_and_range(void)
{
    Stats s = spread(2000, 10);
    TEST_ASSERT_EQUAL_UINT32(100, s.count);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, static_cast<float>(s.mean()));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, static_cast<float>(s.sigma()));
    TEST_ASSERT_EQUAL_UINT16(1990, s.min);
    TEST_ASSERT_EQUAL_UINT16(2010, s.max);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, static_cast<float>(Stats().sigma()));
    TEST_ASSERT_TRUE(Stats().empty());
}

void test_stats_merge(void)
{
    Stats a = constant(1000, 10);
    a.merge(constant(3000, 10));
    a.merge(Stats());
    TEST_ASSERT_EQUAL_UINT32(20, a.count);
    TEST_ASSERT_EQUAL_FLOAT(2000.0f, static_cast<float>(a.mean()));
    TEST_ASSERT_EQUAL_UINT16(1000, a.min);
    TEST_ASSERT_EQUAL_UINT16(3000, a.max);
}

void test_leg_samples_take_the_middle_half(void)
{
    LegSamples samples;
    // 10 s leg: 1000 near the ends, 2000 in the middle half.
    for (uint32_t t = 0; t < 10000; t += 20)
        samples.add(t, t >= 2500 && t < 7500 ? 2000 : 1000);
    const Stats mid = samples.middle(10000);
    TEST_ASSERT_FALSE(mid.empty());
    TEST_ASSERT_EQUAL_UINT16(2000, mid.min);
    TEST_ASSERT_EQUAL_UINT16(2000, mid.max);
}

void test_leg_samples_widen_for_a_long_leg(void)
{
    LegSamples samples;
    for (uint32_t t = 0; t < 120000; t += 20)
        samples.add(t, t >= 30000 && t < 90000 ? 2000 : 1000);
    TEST_ASSERT_TRUE(samples.widthMs() > LegSamples::FIRST_WIDTH_MS);
    TEST_ASSERT_TRUE(samples.widthMs() * LegSamples::BUCKETS > 120000);
    const Stats mid = samples.middle(120000);
    TEST_ASSERT_EQUAL_UINT16(2000, mid.min);
    TEST_ASSERT_TRUE(mid.count > 2000);
    samples.clear();
    TEST_ASSERT_EQUAL_UINT32(LegSamples::FIRST_WIDTH_MS, samples.widthMs());
    TEST_ASSERT_TRUE(samples.middle(1000).empty());
}

void test_leg_samples_of_a_short_leg_take_its_midpoint(void)
{
    LegSamples samples;
    for (uint32_t t = 0; t < 400; t += 20)
        samples.add(t, 1500);
    TEST_ASSERT_FALSE(samples.middle(400).empty());
}

void test_electrical_mean_peak_and_supply(void)
{
    Electrical e;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.ampsMean());
    e.addAmps(2.0f);
    e.addAmps(4.0f);
    e.addAmps(NAN);
    e.addSupply(12.4f);
    e.addSupply(12.1f);
    e.addSupply(NAN);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, e.ampsMean());
    TEST_ASSERT_EQUAL_FLOAT(4.0f, e.ampsPeak);
    TEST_ASSERT_TRUE(e.supplyRead);
    TEST_ASSERT_EQUAL_FLOAT(12.1f, e.supplyMin);
}

// --- Speed and margin ----------------------------------------------------------------

void test_speed_from_the_slow_legs(void)
{
    const Proposal p = propose(good(41.0f, 41.0f), Roof::Settings());
    TEST_ASSERT_TRUE(p.speed);
    TEST_ASSERT_FLOAT_WITHIN(0.051f, 41.0f, p.speedMmPerSecond);
    TEST_ASSERT_EQUAL_UINT32(1850, p.travelMm);
    TEST_ASSERT_TRUE(p.warnings.empty());
}

void test_speed_is_rounded_to_a_tenth(void)
{
    const Proposal p = propose(good(38.04f, 38.04f), Roof::Settings());
    TEST_ASSERT_EQUAL_FLOAT(38.0f, p.speedMmPerSecond);
}

void test_speed_from_the_top_legs_when_run(void)
{
    Measurements m = good(30.0f, 30.0f); // slower per duty at 40 %
    m.legs[2] = legAt(LegKind::TopOpen, 100, 38.0f, 1850);
    m.legs[3] = legAt(LegKind::TopClose, 100, 38.0f, 1850);
    m.legCount = 4;
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_FLOAT_WITHIN(0.051f, 38.0f, p.speedMmPerSecond);
    // The margin covers the slow legs taking longer than 38 mm/s predicts.
    const double slowMs = m.legs[0].drivenMs;
    const double shortfall = slowMs - slowMs * 30.0 / 38.0;
    TEST_ASSERT_TRUE(p.timeoutMarginSeconds * 1000.0 >= 1.5 * shortfall + 3000.0);
}

void test_disagreeing_legs_withhold_the_speed(void)
{
    const Proposal p = propose(good(38.0f, 26.0f), Roof::Settings());
    TEST_ASSERT_FALSE(p.speed);
    TEST_ASSERT_EQUAL_STRING("legsDisagree", p.speedProblem.reason.c_str());
    TEST_ASSERT_TRUE(p.hall); // the thresholds stand on their own
}

void test_legs_disagreeing_a_little_are_warned_about(void)
{
    const Proposal p = propose(good(38.0f, 32.0f), Roof::Settings());
    TEST_ASSERT_TRUE(p.speed);
    TEST_ASSERT_EQUAL(1, p.warnings.size());
    TEST_ASSERT_EQUAL_STRING("directionsDiffer", p.warnings[0].reason.c_str());
}

void test_a_too_short_leg_withholds_the_speed(void)
{
    Measurements m = good();
    m.legs[1].drivenMs = MIN_MEASURED_LEG_MS - 1;
    m.legs[1].dutyMs = m.legs[1].drivenMs * 0.4f;
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_FALSE(p.speed);
    TEST_ASSERT_EQUAL_STRING("legTooShort", p.speedProblem.reason.c_str());
    m.legs[1] = legAt(LegKind::SlowClose, 40, 38.0f, 1850);
    m.legs[1].drivenMs = MIN_MEASURED_LEG_MS;
    m.legs[0].drivenMs = MIN_MEASURED_LEG_MS;
    m.legs[0].dutyMs = m.legs[1].dutyMs = MIN_MEASURED_LEG_MS * 0.4f;
    TEST_ASSERT_NOT_EQUAL(0, std::string(propose(m, Roof::Settings()).speedProblem.reason).compare("legTooShort"));
}

void test_a_speed_out_of_range_is_withheld(void)
{
    Measurements m = good();
    m.travelMm = 100000; // implies far over 500 mm/s
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_FALSE(p.speed);
    TEST_ASSERT_EQUAL_STRING("outOfRange", p.speedProblem.reason.c_str());
}

void test_the_margin_is_at_least_five_seconds_and_a_fifth_of_a_move(void)
{
    const Proposal p = propose(good(), Roof::Settings());
    Roof::Settings s;
    const double move = Roof::expectedMoveMs(s, 1850.0f);
    const uint32_t expected = static_cast<uint32_t>(std::ceil(std::max(5000.0, std::max(0.2 * move, 3000.0)) / 1000.0));
    TEST_ASSERT_EQUAL_UINT32(expected, p.timeoutMarginSeconds);
    Measurements fast = good(400.0f, 400.0f);
    fast.travelMm = 1850;
    TEST_ASSERT_EQUAL_UINT32(5, propose(fast, Roof::Settings()).timeoutMarginSeconds);
}

void test_the_margin_stays_in_its_range(void)
{
    Measurements m = good(1.2f, 1.2f); // a very slow roof: a long move
    m.travelMm = 1850;
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_TRUE(p.speed);
    TEST_ASSERT_EQUAL_UINT32(RoofConfig::MAX_TIMEOUT_MARGIN_SECONDS, p.timeoutMarginSeconds);
}

// --- Polarity and thresholds ------------------------------------------------------------

void test_good_thresholds_are_kept(void)
{
    const Proposal p = propose(good(), Roof::Settings());
    TEST_ASSERT_TRUE(p.hall);
    TEST_ASSERT_TRUE(p.hallHighEnd == End::Open);
    TEST_ASSERT_EQUAL_UINT16(3000, p.hallHighCounts);
    TEST_ASSERT_EQUAL_UINT16(1100, p.hallLowCounts);
    TEST_ASSERT_TRUE(p.hallProblem.reason.empty());
}

void test_the_closed_end_reading_high_is_proposed(void)
{
    Measurements m = good();
    m.closedRest = constant(3400);
    m.openRest = constant(700);
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_TRUE(p.hallHighEnd == End::Closed);
}

void test_a_threshold_too_close_to_the_noise_moves_out(void)
{
    Measurements m = good();
    m.between = spread(2050, 50); // 2000-2100, sigma 50: clearance 200
    Roof::Settings old;
    old.hallHighCounts = 2200;
    old.hallLowCounts = 1950;
    const Proposal p = propose(m, old);
    TEST_ASSERT_TRUE(p.hall);
    TEST_ASSERT_EQUAL_UINT16(2300, p.hallHighCounts);
    TEST_ASSERT_EQUAL_UINT16(1800, p.hallLowCounts);
}

void test_a_threshold_not_read_at_rest_moves_in(void)
{
    Measurements m = good();
    m.openRest = constant(3010);    // the roof stops just past 3000
    m.closedRest = spread(1095, 3); // 1092-1098, sigma 3: margin 10
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_TRUE(p.hall);
    TEST_ASSERT_EQUAL_UINT16(3000, p.hallHighCounts); // 3010 - 10
    TEST_ASSERT_EQUAL_UINT16(1108, p.hallLowCounts);  // 1098 + 10
}

void test_noisy_rest_readings_widen_the_rest_margin(void)
{
    Measurements m = good();
    m.openRest = spread(3100, 20); // 3080-3120, sigma 20: margin 60
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_EQUAL_UINT16(3000, p.hallHighCounts); // 3080 - 60 = 3020 >= 3000: kept
    m.openRest = spread(3050, 20);                    // 3030-3070: up to 2970
    TEST_ASSERT_EQUAL_UINT16(2970, propose(m, Roof::Settings()).hallHighCounts);
}

void test_no_room_for_a_threshold_withholds_them(void)
{
    Measurements m = good();
    m.between = spread(2050, 100); // up to 2150, sigma 100: clearance 400 -> at least 2550
    m.openRest = constant(2500);   // read at rest only up to 2490
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_FALSE(p.hall);
    TEST_ASSERT_EQUAL_STRING("noRoom", p.hallProblem.reason.c_str());
    TEST_ASSERT_TRUE(p.speed);
}

void test_both_ends_on_one_side_is_the_same_pole(void)
{
    Measurements m = good();
    m.closedRest = constant(3350);
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_FALSE(p.hall);
    TEST_ASSERT_EQUAL_STRING("samePole", p.hallProblem.reason.c_str());
    TEST_ASSERT_TRUE(p.hallProblem.text.find("same pole") != std::string::npos);
}

void test_an_end_too_close_to_the_in_between_level_is_weak(void)
{
    Measurements m = good();
    m.openRest = constant(2349); // 299 from 2050
    Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_FALSE(p.hall);
    TEST_ASSERT_EQUAL_STRING("weakEnd", p.hallProblem.reason.c_str());
    TEST_ASSERT_TRUE(p.hallProblem.text.find("open end") != std::string::npos);
    m.openRest = constant(2350); // exactly 300: enough
    p = propose(m, Roof::Settings());
    TEST_ASSERT_TRUE(p.hallProblem.reason != "weakEnd");
}

void test_missing_readings_withhold_the_thresholds(void)
{
    Measurements m = good();
    m.openRest = Stats();
    TEST_ASSERT_FALSE(propose(m, Roof::Settings()).hall);
}

void test_thresholds_stay_inside_the_settings_range(void)
{
    Measurements m = good();
    m.closedRest = constant(40);
    m.openRest = constant(4060);
    Roof::Settings old;
    old.hallHighCounts = 3995;
    old.hallLowCounts = 100;
    const Proposal p = propose(m, old);
    TEST_ASSERT_TRUE(p.hall);
    TEST_ASSERT_TRUE(p.hallHighCounts <= RoofConfig::MAX_HALL_COUNTS);
    TEST_ASSERT_TRUE(p.hallLowCounts >= RoofConfig::MIN_HALL_COUNTS);
}

// --- Current ----------------------------------------------------------------------------

void test_a_peak_current_near_the_limit_is_warned_about(void)
{
    Measurements m = good();
    m.currentSense = true;
    m.maxCurrentAmps = 10.0f;
    m.legs[0].electrical.addAmps(7.9f);
    TEST_ASSERT_TRUE(propose(m, Roof::Settings()).warnings.empty());
    m.legs[1].electrical.addAmps(8.0f);
    const Proposal p = propose(m, Roof::Settings());
    TEST_ASSERT_EQUAL(1, p.warnings.size());
    TEST_ASSERT_EQUAL_STRING("currentNearLimit", p.warnings[0].reason.c_str());
    m.currentSense = false;
    TEST_ASSERT_TRUE(propose(m, Roof::Settings()).warnings.empty());
}

void test_leg_names(void)
{
    TEST_ASSERT_EQUAL_STRING("toClosed", legName(LegKind::ToClosed));
    TEST_ASSERT_EQUAL_STRING("slowOpen", legName(LegKind::SlowOpen));
    TEST_ASSERT_EQUAL_STRING("slowClose", legName(LegKind::SlowClose));
    TEST_ASSERT_EQUAL_STRING("topOpen", legName(LegKind::TopOpen));
    TEST_ASSERT_EQUAL_STRING("topClose", legName(LegKind::TopClose));
    TEST_ASSERT_EQUAL_STRING("unknown", legName(static_cast<LegKind>(9)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, legSpeed(MeasuredLeg(), 1850));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_stats_mean_sigma_and_range);
    RUN_TEST(test_stats_merge);
    RUN_TEST(test_leg_samples_take_the_middle_half);
    RUN_TEST(test_leg_samples_widen_for_a_long_leg);
    RUN_TEST(test_leg_samples_of_a_short_leg_take_its_midpoint);
    RUN_TEST(test_electrical_mean_peak_and_supply);
    RUN_TEST(test_speed_from_the_slow_legs);
    RUN_TEST(test_speed_is_rounded_to_a_tenth);
    RUN_TEST(test_speed_from_the_top_legs_when_run);
    RUN_TEST(test_disagreeing_legs_withhold_the_speed);
    RUN_TEST(test_legs_disagreeing_a_little_are_warned_about);
    RUN_TEST(test_a_too_short_leg_withholds_the_speed);
    RUN_TEST(test_a_speed_out_of_range_is_withheld);
    RUN_TEST(test_the_margin_is_at_least_five_seconds_and_a_fifth_of_a_move);
    RUN_TEST(test_the_margin_stays_in_its_range);
    RUN_TEST(test_good_thresholds_are_kept);
    RUN_TEST(test_the_closed_end_reading_high_is_proposed);
    RUN_TEST(test_a_threshold_too_close_to_the_noise_moves_out);
    RUN_TEST(test_a_threshold_not_read_at_rest_moves_in);
    RUN_TEST(test_noisy_rest_readings_widen_the_rest_margin);
    RUN_TEST(test_no_room_for_a_threshold_withholds_them);
    RUN_TEST(test_both_ends_on_one_side_is_the_same_pole);
    RUN_TEST(test_an_end_too_close_to_the_in_between_level_is_weak);
    RUN_TEST(test_missing_readings_withhold_the_thresholds);
    RUN_TEST(test_thresholds_stay_inside_the_settings_range);
    RUN_TEST(test_a_peak_current_near_the_limit_is_warned_about);
    RUN_TEST(test_leg_names);
    return UNITY_END();
}
