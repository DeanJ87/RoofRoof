// The analog front end as settings (spec 023, Advanced): dividers, trims,
// presets, the 3.3 V warnings, the scaling and the supply calibration.

#include <cmath>
#include <cstring>
#include <unity.h>

#include "Analog.h"

using namespace SQM::Board;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    bool hasIssue(const std::vector<AnalogIssue> &issues, const char *input, bool error)
    {
        for (const AnalogIssue &issue : issues)
            if (issue.input == input && issue.error == error)
                return true;
        return false;
    }

    // The roof's gates with current sense on, as RoofConfig's defaults.
    AnalogGates senseOn()
    {
        AnalogGates g;
        g.currentSense = true;
        return g;
    }

    std::vector<AnalogIssue> check(const AnalogConfig &a)
    {
        return checkAnalog(a, AnalogGates{});
    }
} // namespace

void test_the_devkit_bench_preset_is_the_old_scaling(void)
{
    const AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    TEST_ASSERT_EQUAL_FLOAT(122.0f / 22.0f, a.supply.ratio());
    TEST_ASSERT_EQUAL_FLOAT(1.5f, a.currentR.ratio());
    TEST_ASSERT_EQUAL_FLOAT(1.5f, a.currentL.ratio());
    // 2.2 V at the pin is 12.2 V, as before.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.2f, scaledSupplyVolts(2200.0f, a));
    // 100 mV at the pin through 10k/20k at 8.5 A/V.
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.275f, scaledSenseAmps(100.0f, a.currentR, a.currentRTrim, 8.5f));
}

void test_the_wt32_carrier_adds_its_diode_drop(void)
{
    const AnalogConfig a = analogPreset(AnalogPreset::Wt32CarrierV11);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, scaledSupplyVolts(2200.0f, a));
    TEST_ASSERT_FALSE(hasIssue(check(a), "supply", true));
}

void test_a_22k_where_20k_was_drawn_reads_true(void)
{
    // The owner's bench: 10k/22k on R_IS instead of 10k/20k.
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentR = Divider{10000, 22000};
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 32.0f / 22.0f, a.currentR.ratio());
    TEST_ASSERT_FALSE(hasIssue(check(a), "currentR", true));
}

void test_dividers_that_could_exceed_3v3_warn(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.hall = Divider{1000, 100000}; // 4.2 V nearly undivided
    a.currentL = Divider{100, 100000};
    a.supply = Divider{100000, 47000}; // 15 V -> 4.8 V
    const std::vector<AnalogIssue> issues = check(a);
    TEST_ASSERT_TRUE(hasIssue(issues, "hall", false));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentL", false));
    TEST_ASSERT_TRUE(hasIssue(issues, "supply", false));
}

// 10k/20k puts 3.33 V on the pin for the BTS7960's 5 V IS: warned (never an error).
void test_the_drawn_is_divider_is_warned(void)
{
    const std::vector<AnalogIssue> issues = check(analogPreset(AnalogPreset::DevkitBench));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentR", false));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentL", false));
    TEST_ASSERT_FALSE(hasIssue(issues, "supply", false));
    TEST_ASSERT_FALSE(hasIssue(issues, "hall", false));
    for (const AnalogIssue &issue : issues)
        TEST_ASSERT_FALSE(issue.error);
}

void test_bad_values_are_errors(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.supply = Divider{100000, 0};
    a.supplyTrim.gain = 3.0f;
    a.currentRTrim.offset = 0.6f;
    a.currentLTrim.gain = NAN;
    const std::vector<AnalogIssue> issues = check(a);
    TEST_ASSERT_TRUE(hasIssue(issues, "supply", true));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentR", true));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentL", true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.supply.ratio());
}

void test_named_presets_are_fixed(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.supplyTrim.gain = 1.03f;
    TEST_ASSERT_TRUE(hasIssue(check(a), "", true));
    a.preset = AnalogPreset::Custom;
    TEST_ASSERT_FALSE(hasIssue(check(a), "", true));
}

void test_names_round_trip(void)
{
    for (AnalogPreset p : {AnalogPreset::DevkitBench, AnalogPreset::Wt32CarrierV11, AnalogPreset::Custom})
    {
        AnalogPreset back = AnalogPreset::Custom;
        TEST_ASSERT_TRUE(parseAnalogPreset(analogPresetName(p), back));
        TEST_ASSERT_TRUE(back == p);
    }
    AnalogPreset p;
    TEST_ASSERT_FALSE(parseAnalogPreset("wt32", p));
}

void test_supply_calibration_to_a_measured_voltage(void)
{
    const AnalogConfig bench = analogPreset(AnalogPreset::DevkitBench);
    const AnalogConfig wt32 = analogPreset(AnalogPreset::Wt32CarrierV11);
    // Reads 11.6 V, the meter says 12.0 V: the gain becomes 12/11.6.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.0f / 11.6f, calibratedSupplyGain(bench, bench, 11.6f, 12.0f));
    // With the carrier's diode offset kept: (12.3-0.3)/(11.9-0.3).
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.0f / 11.6f, calibratedSupplyGain(wt32, wt32, 11.9f, 12.3f));
    // An already-trimmed reading: the new gain replaces it.
    AnalogConfig trimmed = bench;
    trimmed.supplyTrim.gain = 1.1f;
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.1f, calibratedSupplyGain(trimmed, trimmed, 11.0f, 11.0f));
    // Nothing to work from.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibratedSupplyGain(bench, bench, 0.0f, 12.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibratedSupplyGain(bench, bench, NAN, 12.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibratedSupplyGain(bench, bench, 12.0f, NAN));
}

// The device's reading was scaled with the saved setup; an unsaved edit (here
// the WT32 preset's +0.3 V, or another divider) must not skew the new gain.
void test_calibration_uses_the_saved_setup_for_the_reading(void)
{
    const AnalogConfig saved = analogPreset(AnalogPreset::DevkitBench);
    AnalogConfig edited = analogPreset(AnalogPreset::Wt32CarrierV11);
    // Reads 12.0 V (no offset saved), the meter says 12.0 V: with +0.3 V now added, the gain is (12-0.3)/12.
    const float gain = calibratedSupplyGain(saved, edited, 12.0f, 12.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 11.7f / 12.0f, gain);
    edited.supplyTrim.gain = gain;
    const float pinMv = 12000.0f / saved.supply.ratio();
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.0f, scaledSupplyVolts(pinMv, edited));
    // An unsaved divider: 100k/20k in place of 100k/22k.
    edited = saved;
    edited.preset = AnalogPreset::Custom;
    edited.supply = Divider{100000, 20000};
    edited.supplyTrim.gain = calibratedSupplyGain(saved, edited, 12.0f, 12.2f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.2f, scaledSupplyVolts(pinMv, edited));
}

void test_trim_ranges(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.supplyTrim.gain = 0.8f;
    a.currentRTrim.gain = 1.25f;
    a.currentLTrim.offset = -0.5f;
    TEST_ASSERT_FALSE(hasIssue(check(a), "currentR", true));
    TEST_ASSERT_FALSE(hasIssue(check(a), "currentL", true));
    TEST_ASSERT_FALSE(hasIssue(check(a), "supply", true));
    a.supplyTrim.gain = 0.79f;
    a.currentRTrim.gain = 1.26f;
    a.currentLTrim.offset = -0.51f; // an idle input would read below -0.5 A: unreadable, every move locked
    TEST_ASSERT_TRUE(hasIssue(check(a), "supply", true));
    TEST_ASSERT_TRUE(hasIssue(check(a), "currentR", true));
    TEST_ASSERT_TRUE(hasIssue(check(a), "currentL", true));
    a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.supplyTrim.offset = 1.01f;
    TEST_ASSERT_TRUE(hasIssue(check(a), "supply", true));
}

// With current sense on, the highest reading must reach the current limit.
void test_a_trim_cant_stop_over_current_tripping(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    // 2.9 V x 1.5 x 8.5 A/V = 37.0 A at most.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.975f, highestSenseAmps(a.currentR, a.currentRTrim, 8.5f));
    AnalogGates g = senseOn();
    g.maxCurrentAmps = 29.0f;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentR", true));
    a.preset = AnalogPreset::Custom;
    a.currentRTrim = Trim{0.8f, -0.1f}; // 29.5 A
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentR", true));
    a.currentR = Divider{10000, 40000}; // ratio 1.25: 24.6 A
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentR", true));
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentL", true));
    // A lower A/V does the same; and with current sense off nothing is compared.
    g.ampsPerVolt = 5.0f; // 21.75 A
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentL", true));
    g.currentSense = false;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentL", true));
}

// The IS output tops out at 5 V: a large divider ratio can't read past it.
void test_the_reach_stops_at_the_is_output_top(void)
{
    // 10k/10k (x2): the ADC's 2.9 V would be 5.8 V in, but IS gives 5 V at most: 5 x 8.5 x 0.9 = 38.25 A.
    const Divider d{10000, 10000};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 38.25f, highestSenseAmps(d, Trim{0.9f, 0.0f}, 8.5f));
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentR = d;
    a.currentRTrim.gain = 0.9f;
    AnalogGates g = senseOn();
    g.maxCurrentAmps = 39.0f;
    g.ampsPerVolt = 8.5f;
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentR", true));
    TEST_ASSERT_TRUE(std::isnan(highestSenseAmps(Divider{1000, 0}, Trim{}, 8.5f)));
}

// While driving, the reading is scaled back by the duty, so the offset weighs
// 100 / slow duty times: a broken sense wire reads it, at the slow duty.
void test_a_current_offset_cant_hide_a_broken_wire(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentLTrim.offset = 0.2f;
    AnalogGates g = senseOn();
    g.slowDutyPercent = 40.0f; // 0.5 A while driving
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentL", true));
    g.minRunningAmps = 0.5f;
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentL", true));
    g.minRunningAmps = 1.0f;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentL", true));
    g.slowDutyPercent = 20.0f; // 1.0 A: a broken wire would pass
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentL", true));
}

// A negative offset, scaled by the duty, must neither read as unreadable nor move the over-current point far.
void test_a_negative_offset_is_bounded_at_the_slow_duty(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentRTrim.offset = -0.2f;
    AnalogGates g = senseOn();
    g.slowDutyPercent = 40.0f; // -0.5 A
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentR", true));
    g.slowDutyPercent = 10.0f; // -2 A
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentR", true));
    g.currentSense = false;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentR", true));
}

// The duty is a whole step of 255: 19 % drives at 48, so the offset weighs 255/48, not 100/19.
void test_the_driven_offset_uses_the_duty_as_driven(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentLTrim.offset = 0.1899f; // 0.9995 A by 100/19, 1.0088 A as driven
    AnalogGates g = senseOn();
    g.slowDutyPercent = 19.0f;
    g.minRunningAmps = 1.0f;
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentL", true));
    a.currentLTrim.offset = 0.18f; // 0.956 A
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentL", true));
    // 70 % drives at 178 (float 0.7 x 255 = 178.4999), not 179: +0.5 A reads 0.7163 A.
    a.currentLTrim.offset = 0.5f;
    g.slowDutyPercent = 70.0f;
    g.minRunningAmps = 0.715f;
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentL", true));
}

// A negative offset lowers the saturated reading more at the slow duty: the reach takes the worse.
void test_the_reach_counts_the_offset_at_the_slow_duty(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentR = Divider{100, 100};      // x2: IS's 5 V caps it at 10.4 A with gain 1.04 at 2 A/V
    a.currentRTrim = Trim{1.04f, -0.2f}; // 10.2 A at full duty, 9.9 A at a 40 % slow duty
    AnalogGates g = senseOn();
    g.ampsPerVolt = 2.0f;
    g.maxCurrentAmps = 10.0f;
    g.slowDutyPercent = 40.0f;
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "currentR", true));
    g.maxCurrentAmps = 9.8f;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "currentR", true));
}

// The supply trim may raise a reading at the low-supply limit by 1 V at most.
void test_a_supply_trim_cant_hide_a_low_supply(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::Wt32CarrierV11);
    AnalogGates g;
    g.minSupplyVolts = 15.0f;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "supply", true)); // +0.3 V
    a.preset = AnalogPreset::Custom;
    a.supplyTrim.gain = 1.05f; // +0.75 + 0.3 at 15 V
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "supply", true));
    g.minSupplyVolts = 11.0f; // +0.55 + 0.3
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "supply", true));
    a.supplyTrim = Trim{1.25f, -1.0f}; // a gain up and the offset down: +1.75 at 11 V
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a, g), "supply", true));
    a.supplyTrim = Trim{0.8f, 1.0f}; // reads low: allowed
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a, g), "supply", true));
}

void test_a_named_preset_matches_within_a_tolerance(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::Wt32CarrierV11);
    a.supplyTrim.offset = 0.30001f;
    a.currentRTrim.gain = 0.99999f;
    TEST_ASSERT_FALSE(hasIssue(check(a), "", true));
    a.supplyTrim.offset = 0.301f;
    TEST_ASSERT_TRUE(hasIssue(check(a), "", true));
}

void test_an_unusable_divider_reads_unreadable(void)
{
    Divider none{10000, 0};
    TEST_ASSERT_TRUE(std::isnan(scaledSenseAmps(500.0f, none, Trim{}, 8.5f)));
    AnalogConfig a;
    a.supply = none;
    TEST_ASSERT_TRUE(std::isnan(scaledSupplyVolts(2200.0f, a)));
    // The sum of two large values doesn't wrap.
    TEST_ASSERT_EQUAL_FLOAT(2.0f, (Divider{4000000000U, 4000000000U}).ratio());
}

void test_nan_in_nan_out(void)
{
    const AnalogConfig a;
    TEST_ASSERT_TRUE(std::isnan(scaledSupplyVolts(NAN, a)));
    TEST_ASSERT_TRUE(std::isnan(scaledSenseAmps(NAN, a.currentR, a.currentRTrim, 8.5f)));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_devkit_bench_preset_is_the_old_scaling);
    RUN_TEST(test_the_wt32_carrier_adds_its_diode_drop);
    RUN_TEST(test_a_22k_where_20k_was_drawn_reads_true);
    RUN_TEST(test_dividers_that_could_exceed_3v3_warn);
    RUN_TEST(test_the_drawn_is_divider_is_warned);
    RUN_TEST(test_bad_values_are_errors);
    RUN_TEST(test_named_presets_are_fixed);
    RUN_TEST(test_names_round_trip);
    RUN_TEST(test_supply_calibration_to_a_measured_voltage);
    RUN_TEST(test_calibration_uses_the_saved_setup_for_the_reading);
    RUN_TEST(test_trim_ranges);
    RUN_TEST(test_a_trim_cant_stop_over_current_tripping);
    RUN_TEST(test_the_reach_stops_at_the_is_output_top);
    RUN_TEST(test_a_current_offset_cant_hide_a_broken_wire);
    RUN_TEST(test_a_negative_offset_is_bounded_at_the_slow_duty);
    RUN_TEST(test_the_driven_offset_uses_the_duty_as_driven);
    RUN_TEST(test_the_reach_counts_the_offset_at_the_slow_duty);
    RUN_TEST(test_a_supply_trim_cant_hide_a_low_supply);
    RUN_TEST(test_a_named_preset_matches_within_a_tolerance);
    RUN_TEST(test_an_unusable_divider_reads_unreadable);
    RUN_TEST(test_nan_in_nan_out);
    return UNITY_END();
}
