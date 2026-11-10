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

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_drops_enable_and_both_pwm);
    RUN_TEST(test_forward_drives_rpwm_only);
    RUN_TEST(test_reverse_drives_lpwm_only);
    RUN_TEST(test_zero_duty_is_stopped_not_off);
    RUN_TEST(test_never_both_pwm_for_any_command_or_duty);
    RUN_TEST(test_unknown_command_is_off);
    return UNITY_END();
}
