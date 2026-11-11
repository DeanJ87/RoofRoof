// The park and weather gates every roof command passes (constitution II:
// close only onto a confirmed park, re-checked throughout the close; III: no
// opening unless the weather is safe) and the motion-settings save guard
// (constitution I). Spec 003 FR-016 to FR-020.

#include <unity.h>

#include "Config.h"
#include "RoofGates.h"
#include "SettingsGuard.h"

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

    const Roof::State ALL_STATES[] = {Roof::State::Unknown, Roof::State::Closed,  Roof::State::Open,  Roof::State::Opening,
                                      Roof::State::Closing, Roof::State::Stopped, Roof::State::Error};
} // namespace

void test_open_needs_a_safe_verdict(void)
{
    GateInputs in;
    in.weather = Safety::Verdict::Safe;
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Open, in));
    in.weather = Safety::Verdict::Unsafe;
    TEST_ASSERT_EQUAL(GateRefusal::WeatherNotSafe, gateCommand(Roof::Command::Open, in));
    in.weather = Safety::Verdict::Unknown;
    TEST_ASSERT_EQUAL(GateRefusal::WeatherNotSafe, gateCommand(Roof::Command::Open, in));
}

void test_open_ignores_the_park(void)
{
    GateInputs in;
    in.weather = Safety::Verdict::Safe;
    in.parked = false;
    in.fittedSwitches = 2;
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Open, in));
}

void test_close_needs_park_result_and_fitted_switches(void)
{
    for (uint8_t fitted = 0; fitted <= 2; ++fitted)
    {
        GateInputs in = parkedWith(fitted);
        TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Close, in));
        in.parked = false;
        TEST_ASSERT_EQUAL(GateRefusal::MountNotParked, gateCommand(Roof::Command::Close, in));
    }
    // Each fitted switch must read closed: open and unreadable both refuse.
    for (Contact bad : {Contact::Open, Contact::Unreadable})
    {
        GateInputs one = parkedWith(1);
        one.switch1 = bad;
        TEST_ASSERT_EQUAL(GateRefusal::MountNotParked, gateCommand(Roof::Command::Close, one));
        GateInputs two = parkedWith(2);
        two.switch2 = bad;
        TEST_ASSERT_EQUAL(GateRefusal::MountNotParked, gateCommand(Roof::Command::Close, two));
    }
}

void test_unfitted_switches_are_ignored(void)
{
    GateInputs none = parkedWith(0);
    none.switch1 = Contact::Open;
    none.switch2 = Contact::Unreadable;
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Close, none));
    GateInputs one = parkedWith(1);
    one.switch2 = Contact::Open; // switch 2 not fitted
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Close, one));
    TEST_ASSERT_TRUE(switchesClosed(0, Contact::Unreadable, Contact::Unreadable));
}

void test_close_ignores_the_weather(void)
{
    // Rain must still close the roof (constitution III): the weather never blocks a close.
    GateInputs in = parkedWith(2);
    in.weather = Safety::Verdict::Unsafe;
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Close, in));
}

void test_stop_and_abort_are_never_gated(void)
{
    GateInputs worst;
    worst.weather = Safety::Verdict::Unsafe;
    worst.fittedSwitches = 2;
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Stop, worst));
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Roof::Command::Abort, worst));
}

void test_interlock_lost_only_while_closing(void)
{
    GateInputs lost = parkedWith(1);
    lost.switch1 = Contact::Open;
    const GateInputs held = parkedWith(1);
    for (Roof::State state : ALL_STATES)
    {
        const bool closing = state == Roof::State::Closing;
        TEST_ASSERT_EQUAL_MESSAGE(closing, interlockLost(inState(state), lost), Roof::stateName(state));
        TEST_ASSERT_FALSE(interlockLost(inState(state), held));
    }
    GateInputs notParked = parkedWith(0);
    notParked.parked = false;
    TEST_ASSERT_TRUE(interlockLost(inState(Roof::State::Closing), notParked));
}

void test_interlock_bits_name_the_input(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, interlockBits(parkedWith(2)));
    GateInputs in = parkedWith(2);
    in.parked = false;
    in.switch1 = Contact::Unreadable;
    in.switch2 = Contact::Open;
    TEST_ASSERT_EQUAL_UINT32(InterlockParkResult | InterlockSwitch1 | InterlockSwitch2, interlockBits(in));
    GateInputs one = parkedWith(1);
    one.switch2 = Contact::Open; // not fitted: not named
    TEST_ASSERT_EQUAL_UINT32(0, interlockBits(one));
}

void test_open_stops_when_not_safe(void)
{
    GateInputs unsafe;
    unsafe.weather = Safety::Verdict::Unsafe;
    GateInputs safe;
    safe.weather = Safety::Verdict::Safe;
    for (Roof::State state : ALL_STATES)
    {
        TEST_ASSERT_EQUAL_MESSAGE(state == Roof::State::Opening, openMustStop(inState(state), unsafe), Roof::stateName(state));
        TEST_ASSERT_FALSE(openMustStop(inState(state), safe));
    }
}

void test_stale_safety_handover_is_not_safe_and_not_parked(void)
{
    const GateInputs fresh = parkedWith(0);
    const GateInputs kept = currentGates(fresh, true, SAFETY_HANDOVER_STALE_MS);
    TEST_ASSERT_EQUAL(Safety::Verdict::Safe, kept.weather);
    TEST_ASSERT_TRUE(kept.parked);
    const GateInputs stale = currentGates(fresh, true, SAFETY_HANDOVER_STALE_MS + 1);
    TEST_ASSERT_EQUAL(Safety::Verdict::Unknown, stale.weather);
    TEST_ASSERT_FALSE(stale.parked);
    TEST_ASSERT_EQUAL_STRING("safety inputs not updated for 5.0 s > 5 s", stale.parkText.c_str());
    TEST_ASSERT_EQUAL(GateRefusal::WeatherNotSafe, gateCommand(Roof::Command::Open, stale));
    TEST_ASSERT_EQUAL(GateRefusal::MountNotParked, gateCommand(Roof::Command::Close, stale));
    Roof::Status closing;
    closing.state = Roof::State::Closing;
    TEST_ASSERT_TRUE(interlockLost(closing, stale));
    const GateInputs never = currentGates(fresh, false, 0);
    TEST_ASSERT_FALSE(never.parked);
    TEST_ASSERT_EQUAL_STRING("safety inputs not evaluated yet", never.weatherText.c_str());
}

void test_gate_refusal_names(void)
{
    TEST_ASSERT_EQUAL_STRING("none", gateRefusalName(GateRefusal::None));
    TEST_ASSERT_EQUAL_STRING("weatherNotSafe", gateRefusalName(GateRefusal::WeatherNotSafe));
    TEST_ASSERT_EQUAL_STRING("mountNotParked", gateRefusalName(GateRefusal::MountNotParked));
}

// --- Settings save guard ----------------------------------------------------

void test_settings_save_refused_only_while_moving(void)
{
    for (Roof::State state : ALL_STATES)
    {
        const bool moving = state == Roof::State::Opening || state == Roof::State::Closing;
        TEST_ASSERT_EQUAL_MESSAGE(moving, settingsSaveRefused(state, true), Roof::stateName(state));
        TEST_ASSERT_FALSE(settingsSaveRefused(state, false));
    }
}

void test_every_roof_and_park_field_counts_as_a_change(void)
{
    const Config base = Config::createDefault();
    TEST_ASSERT_FALSE(motionSettingsChanged(base, base));
    void (*const changes[])(Config &) = {
        [](Config &c) { c.roof.travelMm += 1; },
        [](Config &c) { c.roof.speedMmPerSecond += 1.0f; },
        [](Config &c) { c.roof.timeoutMarginSeconds += 1; },
        [](Config &c) { c.roof.leaveMagnetSeconds += 1; },
        [](Config &c) { c.roof.rampUpMs += 1; },
        [](Config &c) { c.roof.rampDownMs += 1; },
        [](Config &c) { c.roof.topDutyPercent -= 1; },
        [](Config &c) { c.roof.slowDutyPercent += 1; },
        [](Config &c) { c.roof.openDirection = RoofConfig::Direction::Reverse; },
        [](Config &c) { c.roof.hallHighEnd = RoofConfig::HallEnd::Closed; },
        [](Config &c) { c.roof.hallHighCounts += 1; },
        [](Config &c) { c.roof.hallLowCounts += 1; },
        [](Config &c) { c.roof.currentSenseEnabled = true; },
        [](Config &c) { c.roof.maxCurrentAmps += 1.0f; },
        [](Config &c) { c.roof.currentAmpsPerVolt += 1.0f; },
        [](Config &c) { c.roof.minSupplyVolts += 0.5f; },
        [](Config &c) { c.park.ninaUrl = "http://192.168.1.20:1888"; },
        [](Config &c) { c.park.switches = ParkConfig::Switches::Two; },
    };
    for (auto change : changes)
    {
        Config next = base;
        change(next);
        TEST_ASSERT_TRUE(motionSettingsChanged(base, next));
    }
}

void test_other_sections_are_not_motion_settings(void)
{
    const Config base = Config::createDefault();
    Config next = base;
    next.deviceName = "Observatory";
    next.mqtt.enabled = !base.mqtt.enabled;
    next.safety.staleSeconds = base.safety.staleSeconds + 10;
    next.safety.rainContact = !base.safety.rainContact;
    next.alerts.enabled = !base.alerts.enabled;
    TEST_ASSERT_FALSE(motionSettingsChanged(base, next));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_needs_a_safe_verdict);
    RUN_TEST(test_open_ignores_the_park);
    RUN_TEST(test_close_needs_park_result_and_fitted_switches);
    RUN_TEST(test_unfitted_switches_are_ignored);
    RUN_TEST(test_close_ignores_the_weather);
    RUN_TEST(test_stop_and_abort_are_never_gated);
    RUN_TEST(test_interlock_lost_only_while_closing);
    RUN_TEST(test_interlock_bits_name_the_input);
    RUN_TEST(test_open_stops_when_not_safe);
    RUN_TEST(test_stale_safety_handover_is_not_safe_and_not_parked);
    RUN_TEST(test_gate_refusal_names);
    RUN_TEST(test_settings_save_refused_only_while_moving);
    RUN_TEST(test_every_roof_and_park_field_counts_as_a_change);
    RUN_TEST(test_other_sections_are_not_motion_settings);
    return UNITY_END();
}
