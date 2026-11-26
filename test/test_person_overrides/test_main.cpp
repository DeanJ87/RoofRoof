// Spec 039 (constitution 10.0.0): the owner is the source of truth. A person
// at the authenticated web UI may set the park check aside for one Open, as
// for one Close; the open isn't then stopped by the park interlock, while the
// weather, faults and Stop still stop it. No automation (an Alpaca client,
// MQTT or Home Assistant, the controller's own moves) can carry a person's
// override, whatever it sends: the audit table in specs/039's spec.md, row by row.

#include <unity.h>

#include "AlertEngine.h"
#include "CommandCheck.h"
#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "MoveGuard.h"
#include "RoofAlerts.h"
#include "RoofController.h"
#include "RoofGates.h"

#include "../roof_sim/RoofSim.h"

using namespace SQM;
using namespace SQM::RoofIo;
using Roof::Command;
using Roof::Origin;
using RoofTest::Bench;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint8_t EVERY_BIT = OverridePark | OverrideWeather | OverrideDaylight | OverrideNinaLost;
    const Origin AUTOMATIONS[] = {Origin::Client, Origin::Mqtt, Origin::Automatic};

    GateInputs notParked()
    {
        GateInputs in;
        in.weather = Safety::Verdict::Safe;
        in.weatherText = "safe";
        in.parked = false;
        in.parkText = "N.I.N.A. says the mount is not parked";
        return in;
    }

    GateInputs parkedNow()
    {
        GateInputs in = notParked();
        in.parked = true;
        in.parkText = "parked";
        return in;
    }

    Roof::Status inState(Roof::State state)
    {
        Roof::Status s;
        s.state = state;
        return s;
    }

    CommandContext with(uint8_t overrides)
    {
        CommandContext c;
        c.overrides = overrides;
        return c;
    }
} // namespace

// Audit rows "park (Open)" and "park (Close)": only a person's Open and Close
// may carry the park bit; weather and daylight only a person's Open; the
// N.I.N.A.-lost bit only the automatic close's Close.
void test_who_may_carry_which_override(void)
{
    TEST_ASSERT_EQUAL_UINT8(OverridePark | OverrideWeather | OverrideDaylight, allowedOverrides(Command::Open, Origin::Person, EVERY_BIT));
    TEST_ASSERT_EQUAL_UINT8(OverridePark, allowedOverrides(Command::Close, Origin::Person, EVERY_BIT));
    TEST_ASSERT_EQUAL_UINT8(OverrideNinaLost, allowedOverrides(Command::Close, Origin::Automatic, EVERY_BIT));
    TEST_ASSERT_EQUAL_UINT8(0, allowedOverrides(Command::Open, Origin::Automatic, EVERY_BIT));
    for (Origin o : {Origin::Client, Origin::Mqtt})
        for (Command c : {Command::Open, Command::Close, Command::Stop, Command::Abort})
            TEST_ASSERT_EQUAL_UINT8(0, allowedOverrides(c, o, EVERY_BIT));
    for (Command c : {Command::Stop, Command::Abort})
        TEST_ASSERT_EQUAL_UINT8(0, allowedOverrides(c, Origin::Person, EVERY_BIT));
}

void test_a_persons_open_may_set_the_park_aside(void)
{
    const GateInputs in = notParked();
    TEST_ASSERT_EQUAL(GateRefusal::MountNotParked, gateCommand(Command::Open, in));
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Command::Open, in, OverridePark));
    // Offered in the status only while the park doesn't hold.
    TEST_ASSERT_EQUAL_UINT8(OverridePark, overridableNow(Command::Open, in));
    TEST_ASSERT_EQUAL_UINT8(0, overridableNow(Command::Open, parkedNow()));
    // The park bit sets nothing else aside: the weather, daylight and a suspect switch still refuse.
    GateInputs rain = in;
    rain.weather = Safety::Verdict::Unsafe;
    rain.weatherReasons = Safety::ReasonRain;
    TEST_ASSERT_EQUAL(GateRefusal::WeatherNotSafe, gateCommand(Command::Open, rain, OverridePark));
    GateInputs day = in;
    day.daylightCloses = true;
    TEST_ASSERT_EQUAL(GateRefusal::Daylight, gateCommand(Command::Open, day, OverridePark));
    TEST_ASSERT_EQUAL(GateRefusal::None, gateCommand(Command::Open, day, OverridePark | OverrideDaylight));
    GateInputs stuck = in;
    stuck.stuckSwitch = 1;
    TEST_ASSERT_EQUAL(GateRefusal::SwitchCheck, gateCommand(Command::Open, stuck, OverridePark));
}

// The whole check (RoofIo::checkCommand), the path every origin's command takes.
void test_only_a_person_gets_past_the_park_whatever_is_sent(void)
{
    Bench b; // closed, at its magnet
    const GateInputs in = notParked();
    TEST_ASSERT_TRUE(checkCommand(Command::Open, Origin::Person, in, b.roof, with(OverridePark)).accepted);
    TEST_ASSERT_FALSE(checkCommand(Command::Open, Origin::Person, in, b.roof, with(0)).accepted);
    for (Origin o : AUTOMATIONS)
    {
        const CommandCheck check = checkCommand(Command::Open, o, in, b.roof, with(EVERY_BIT));
        TEST_ASSERT_FALSE(check.accepted);
        TEST_ASSERT_EQUAL_STRING("mountNotParked", check.reason.c_str());
    }
    Bench open(Roof::Settings(), 1850.0f); // open, at its magnet
    TEST_ASSERT_TRUE(checkCommand(Command::Close, Origin::Person, in, open.roof, with(OverridePark)).accepted);
    for (Origin o : AUTOMATIONS)
    {
        // The automatic close's N.I.N.A.-lost bit needs the switches; none fitted counts as closed,
        // so give it a fitted switch that is open: then nothing but a person gets past.
        GateInputs fitted = in;
        fitted.fittedSwitches = 1;
        fitted.switch1 = Contact::Open;
        TEST_ASSERT_FALSE(checkCommand(Command::Close, o, fitted, open.roof, with(EVERY_BIT)).accepted);
    }
}

// Audit: the guard keeps an override only for the origin that may set it, so a
// caller that hands over raw bits (a deferred close, the wait for the park) can't
// give an automation a person's override.
void test_the_guard_keeps_an_override_only_for_whom_it_is_allowed(void)
{
    const GateInputs in = notParked();
    for (Origin o : AUTOMATIONS)
    {
        MoveGuard guard;
        guard.ran(Command::Open, o, EVERY_BIT, in);
        TEST_ASSERT_FALSE(guard.openParkOverride());
        TEST_ASSERT_EQUAL_UINT8(0, guard.openOverride());
        TEST_ASSERT_EQUAL_UINT8(0, guard.inEffect());
        guard.ran(Command::Close, o, OverridePark | OverrideWeather | OverrideDaylight, in);
        TEST_ASSERT_FALSE(guard.closeParkOverride());
        TEST_ASSERT_EQUAL_UINT8(0, guard.inEffect());
    }
    MoveGuard guard;
    guard.ran(Command::Open, Origin::Person, OverridePark, in);
    TEST_ASSERT_TRUE(guard.openParkOverride());
    TEST_ASSERT_FALSE(guard.closeParkOverride());
    TEST_ASSERT_EQUAL_UINT8(OverridePark, guard.inEffect());
    guard.ran(Command::Close, Origin::Person, 0, in); // a close ends it
    TEST_ASSERT_FALSE(guard.openParkOverride());
}

// For that one open: it ends when the roof stops opening, however.
void test_the_open_park_override_lasts_one_open(void)
{
    const GateInputs in = notParked();
    for (Roof::State end : {Roof::State::Open, Roof::State::Stopped, Roof::State::Error, Roof::State::Closing, Roof::State::Unknown})
    {
        MoveGuard guard;
        guard.ran(Command::Open, Origin::Person, OverridePark, in);
        guard.step(inState(Roof::State::Opening), false, in, CommandCheck{}, 0);
        TEST_ASSERT_TRUE(guard.openParkOverride());
        guard.step(inState(end), false, in, CommandCheck{}, 20);
        TEST_ASSERT_FALSE_MESSAGE(guard.openParkOverride(), Roof::stateName(end));
        guard.step(inState(Roof::State::Opening), false, in, CommandCheck{}, 40);
        TEST_ASSERT_FALSE(guard.openParkOverride()); // the next open checks again
    }
}

// The park interlock: not for a person's confirmed open, and only for it.
void test_the_interlock_spares_only_the_overridden_open(void)
{
    const GateInputs in = notParked();
    const Roof::Status opening = inState(Roof::State::Opening);
    TEST_ASSERT_TRUE(interlockLost(opening, in));
    TEST_ASSERT_FALSE(interlockLost(opening, in, false, false, true));
    TEST_ASSERT_TRUE(interlockLost(opening, in, true, false, false));                       // a close's override isn't an open's
    TEST_ASSERT_TRUE(interlockLost(inState(Roof::State::Closing), in, false, false, true)); // nor the other way
    Roof::Status leg = opening;
    leg.calibrating = true;
    TEST_ASSERT_TRUE(interlockLost(leg, in, false, false, true));
    Roof::Status jog = opening; // a jog toward open carries no override
    jog.jogging = true;
    jog.target = Roof::End::Open;
    TEST_ASSERT_TRUE(interlockLost(jog, in, false, false, true));
}

// End to end on the simulated roof: a person opens past the park; the park
// stays unconfirmed all the way and the roof reaches open; the same open with
// the weather turning unsafe is stopped; a client's open never starts.
void test_a_confirmed_open_runs_to_open_and_the_weather_still_stops_it(void)
{
    GateInputs in = notParked();
    {
        Bench b;
        MoveGuard guard;
        TEST_ASSERT_TRUE(checkCommand(Command::Open, Origin::Person, in, b.roof, with(OverridePark)).accepted);
        TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Person).accepted);
        guard.ran(Command::Open, Origin::Person, OverridePark, in);
        bool lost = false;
        bool stop = false;
        for (int i = 0; i < 20000 && b.roof.status().state != Roof::State::Open; ++i)
        {
            b.step();
            guard.step(b.roof.status(), false, in, CommandCheck{}, b.nowMs);
            lost = lost || interlockLost(b.roof.status(), in, guard.closeParkOverride(), guard.closeNinaLost(), guard.openParkOverride());
            stop = stop || openMustStop(b.roof.status(), in, guard.openOverride());
        }
        TEST_ASSERT_EQUAL(Roof::State::Open, b.roof.status().state);
        TEST_ASSERT_FALSE(lost);
        TEST_ASSERT_FALSE(stop);
        TEST_ASSERT_FALSE(guard.openParkOverride()); // over with the open
    }
    {
        Bench b;
        MoveGuard guard;
        TEST_ASSERT_TRUE(b.command(Command::Open, Origin::Person).accepted);
        guard.ran(Command::Open, Origin::Person, OverridePark, in);
        b.steps(100);
        guard.step(b.roof.status(), false, in, CommandCheck{}, b.nowMs);
        TEST_ASSERT_EQUAL(Roof::State::Opening, b.roof.status().state);
        in.weather = Safety::Verdict::Unsafe;
        in.weatherReasons = Safety::ReasonWeatherUnsafe;
        TEST_ASSERT_TRUE(openMustStop(b.roof.status(), in, guard.openOverride())); // weather not overridden
        TEST_ASSERT_FALSE(interlockLost(b.roof.status(), in, false, false, guard.openParkOverride()));
    }
    {
        Bench b;
        for (Origin o : AUTOMATIONS)
            TEST_ASSERT_FALSE(checkCommand(Command::Open, o, notParked(), b.roof, with(OverridePark)).accepted);
    }
}

// Logged and alerted (information level, never critical).
void test_the_override_is_logged_and_alerted(void)
{
    const std::string text = parkOverrideText(Command::Open, notParked());
    TEST_ASSERT_EQUAL_STRING(
        "Open with the park check set aside by a person (Mount not parked: park not confirmed (N.I.N.A. says the mount is not "
        "parked)): the roof passes over the telescope, watch it",
        text.c_str());
    const Alerts::Alert alert = CloseSeq::parkOverrideAlert(text);
    TEST_ASSERT_TRUE(alert.type == Alerts::AlertType::CheckOverridden);
    TEST_ASSERT_TRUE(alert.level == Alerts::AlertLevel::Normal);
    TEST_ASSERT_FALSE(Alerts::isCritical(alert.type));
    TEST_ASSERT_EQUAL_STRING("check_overridden", Alerts::alertTypeName(alert.type));
    TEST_ASSERT_EQUAL_STRING("Park check overridden", alert.title.c_str());
    TEST_ASSERT_EQUAL_STRING((text + ".").c_str(), alert.message.c_str());
}

// The web API's body: an Open may now name the park.
void test_the_open_body_may_name_the_park(void)
{
    uint8_t bits = 0;
    std::string error;
    TEST_ASSERT_TRUE(parseOverrides(R"({"override": ["mountNotParked", "weatherNotSafe"]})", bits, error));
    TEST_ASSERT_EQUAL_UINT8(OverridePark | OverrideWeather, bits);
    TEST_ASSERT_FALSE(parseOverrides(R"({"override": ["rain"]})", bits, error));
    TEST_ASSERT_EQUAL_STRING("Only mountNotParked (Open or Close), weatherNotSafe and daylight (Open) can be overridden", error.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_who_may_carry_which_override);
    RUN_TEST(test_a_persons_open_may_set_the_park_aside);
    RUN_TEST(test_only_a_person_gets_past_the_park_whatever_is_sent);
    RUN_TEST(test_the_guard_keeps_an_override_only_for_whom_it_is_allowed);
    RUN_TEST(test_the_open_park_override_lasts_one_open);
    RUN_TEST(test_the_interlock_spares_only_the_overridden_open);
    RUN_TEST(test_a_confirmed_open_runs_to_open_and_the_weather_still_stops_it);
    RUN_TEST(test_the_override_is_logged_and_alerted);
    RUN_TEST(test_the_open_body_may_name_the_park);
    return UNITY_END();
}
