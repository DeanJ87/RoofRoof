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
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a), "supply", true));
}

void test_a_22k_where_20k_was_drawn_reads_true(void)
{
    // The owner's bench: 10k/22k on R_IS instead of 10k/20k.
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.currentR = Divider{10000, 22000};
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 32.0f / 22.0f, a.currentR.ratio());
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a), "currentR", true));
}

void test_dividers_that_could_exceed_3v3_warn(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.preset = AnalogPreset::Custom;
    a.hall = Divider{1000, 100000}; // 4.2 V nearly undivided
    a.currentL = Divider{100, 100000};
    a.supply = Divider{100000, 47000}; // 15 V -> 4.8 V
    const std::vector<AnalogIssue> issues = checkAnalog(a);
    TEST_ASSERT_TRUE(hasIssue(issues, "hall", false));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentL", false));
    TEST_ASSERT_TRUE(hasIssue(issues, "supply", false));
}

// 10k/20k puts 3.33 V on the pin for the BTS7960's 5 V IS: warned (never an error).
void test_the_drawn_is_divider_is_warned(void)
{
    const std::vector<AnalogIssue> issues = checkAnalog(analogPreset(AnalogPreset::DevkitBench));
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
    a.currentRTrim.offset = 6.0f;
    a.currentLTrim.gain = NAN;
    const std::vector<AnalogIssue> issues = checkAnalog(a);
    TEST_ASSERT_TRUE(hasIssue(issues, "supply", true));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentR", true));
    TEST_ASSERT_TRUE(hasIssue(issues, "currentL", true));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, a.supply.ratio());
}

void test_named_presets_are_fixed(void)
{
    AnalogConfig a = analogPreset(AnalogPreset::DevkitBench);
    a.supplyTrim.gain = 1.03f;
    TEST_ASSERT_TRUE(hasIssue(checkAnalog(a), "", true));
    a.preset = AnalogPreset::Custom;
    TEST_ASSERT_FALSE(hasIssue(checkAnalog(a), "", true));
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
    // Reads 11.6 V, the meter says 12.0 V: the gain becomes 12/11.6.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.0f / 11.6f, calibratedSupplyGain(Trim{}, 11.6f, 12.0f));
    // With the carrier's diode offset kept: (12.3-0.3)/(11.9-0.3).
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 12.0f / 11.6f, calibratedSupplyGain(Trim{1.0f, 0.3f}, 11.9f, 12.3f));
    // An already-trimmed reading: the new gain replaces it.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.1f, calibratedSupplyGain(Trim{1.1f, 0.0f}, 11.0f, 11.0f));
    // Nothing to work from.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibratedSupplyGain(Trim{}, 0.0f, 12.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibratedSupplyGain(Trim{}, NAN, 12.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, calibratedSupplyGain(Trim{}, 12.0f, NAN));
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
    RUN_TEST(test_nan_in_nan_out);
    return UNITY_END();
}
