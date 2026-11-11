// The IBT-2 (BTS7960) driver's output levels (lib/DriverLogic): constitution
// Principle I - RPWM and LPWM are never driven together, and Off is EN low with
// both PWM at 0.

#include <initializer_list>
#include <unity.h>

#include "DriverLogic.h"

using namespace SQM::Driver;

void setUp(void) {}
void tearDown(void) {}

void test_off_drops_enable_and_both_pwm(void)
{
    for (int duty = 0; duty <= 255; ++duty)
    {
        const Outputs out = outputsFor(Command::Off, static_cast<uint8_t>(duty));
        TEST_ASSERT_FALSE(out.enable);
        TEST_ASSERT_EQUAL_UINT8(0, out.rpwm);
        TEST_ASSERT_EQUAL_UINT8(0, out.lpwm);
    }
}

void test_forward_drives_rpwm_only(void)
{
    const Outputs out = outputsFor(Command::Forward, 200);
    TEST_ASSERT_TRUE(out.enable);
    TEST_ASSERT_EQUAL_UINT8(200, out.rpwm);
    TEST_ASSERT_EQUAL_UINT8(0, out.lpwm);
}

void test_reverse_drives_lpwm_only(void)
{
    const Outputs out = outputsFor(Command::Reverse, 90);
    TEST_ASSERT_TRUE(out.enable);
    TEST_ASSERT_EQUAL_UINT8(0, out.rpwm);
    TEST_ASSERT_EQUAL_UINT8(90, out.lpwm);
}

// Duty 0 while enabled: both low, the driver holding the motor stopped.
void test_zero_duty_is_stopped_not_off(void)
{
    for (Command command : {Command::Forward, Command::Reverse})
    {
        const Outputs out = outputsFor(command, 0);
        TEST_ASSERT_TRUE(out.enable);
        TEST_ASSERT_EQUAL_UINT8(0, out.rpwm);
        TEST_ASSERT_EQUAL_UINT8(0, out.lpwm);
    }
}

// The invariant, exhaustively: for every command and duty, never both.
void test_never_both_pwm_for_any_command_or_duty(void)
{
    for (Command command : {Command::Off, Command::Forward, Command::Reverse})
        for (int duty = 0; duty <= 255; ++duty)
        {
            const Outputs out = outputsFor(command, static_cast<uint8_t>(duty));
            TEST_ASSERT_FALSE(out.rpwm > 0 && out.lpwm > 0);
            if (!out.enable)
                TEST_ASSERT_TRUE(out.rpwm == 0 && out.lpwm == 0);
        }
}

// An out-of-range command value (memory corruption, a bad cast) is Off.
void test_unknown_command_is_off(void)
{
    const Outputs out = outputsFor(static_cast<Command>(7), 255);
    TEST_ASSERT_FALSE(out.enable);
    TEST_ASSERT_EQUAL_UINT8(0, out.rpwm);
    TEST_ASSERT_EQUAL_UINT8(0, out.lpwm);
}

// Spec 011 FR-021: a bench build never lets a level reach the pins, for any
// command, duty or hand-made combination of levels.
void test_bench_build_holds_every_pin_low(void)
{
    for (Command command : {Command::Off, Command::Forward, Command::Reverse})
        for (int duty = 0; duty <= 255; ++duty)
        {
            const Outputs pins = physicalOutputs(outputsFor(command, static_cast<uint8_t>(duty)), true);
            TEST_ASSERT_FALSE(pins.enable);
            TEST_ASSERT_EQUAL_UINT8(0, pins.rpwm);
            TEST_ASSERT_EQUAL_UINT8(0, pins.lpwm);
        }
    Outputs raw;
    raw.enable = true;
    raw.rpwm = 255;
    raw.lpwm = 255;
    const Outputs pins = physicalOutputs(raw, true);
    TEST_ASSERT_FALSE(pins.enable);
    TEST_ASSERT_EQUAL_UINT8(0, pins.rpwm);
    TEST_ASSERT_EQUAL_UINT8(0, pins.lpwm);
}

// The standard build applies the logic's levels unchanged.
void test_standard_build_applies_levels_unchanged(void)
{
    for (Command command : {Command::Off, Command::Forward, Command::Reverse})
        for (int duty = 0; duty <= 255; ++duty)
        {
            const Outputs logical = outputsFor(command, static_cast<uint8_t>(duty));
            const Outputs pins = physicalOutputs(logical, false);
            TEST_ASSERT_EQUAL(logical.enable, pins.enable);
            TEST_ASSERT_EQUAL_UINT8(logical.rpwm, pins.rpwm);
            TEST_ASSERT_EQUAL_UINT8(logical.lpwm, pins.lpwm);
        }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_drops_enable_and_both_pwm);
    RUN_TEST(test_forward_drives_rpwm_only);
    RUN_TEST(test_reverse_drives_lpwm_only);
    RUN_TEST(test_zero_duty_is_stopped_not_off);
    RUN_TEST(test_never_both_pwm_for_any_command_or_duty);
    RUN_TEST(test_unknown_command_is_off);
    RUN_TEST(test_bench_build_holds_every_pin_low);
    RUN_TEST(test_standard_build_applies_levels_unchanged);
    return UNITY_END();
}
