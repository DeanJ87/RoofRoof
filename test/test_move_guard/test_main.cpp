// Spec 027 (constitution 7.0.0): a command refused only for the park waits
// for it when N.I.N.A. can be asked; a person's overrides take effect and end.

#include <unity.h>

#include "MoveGuard.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const CommandCheck OK{};
    const CommandCheck NOT_PARKED{false, "mountNotParked", "Mount not parked: park result"};
    const CommandCheck WEATHER{false, "weatherNotSafe", "Weather not safe: rain"};

    MoveGuard guardWith(bool ninaSet, bool requestPark)
    {
        MoveGuard guard;
        MoveGuardSettings s;
        s.ninaSet = ninaSet;
        s.requestPark = requestPark;
        s.wait.limitMs = 10000;
        s.wait.holdMs = 1000;
        guard.configure(s);
        return guard;
    }

    GateInputs parked(bool yes)
    {
        GateInputs in;
        in.weather = Safety::Verdict::Safe;
        in.parked = yes;
        return in;
    }

    Roof::Status inState(Roof::State state)
    {
        Roof::Status s;
        s.state = state;
        return s;
    }

    // Spec 043: at rest on a magnet, where a wait may ask N.I.N.A. to park.
    const Roof::Status AT_END = inState(Roof::State::Closed);
} // namespace

void test_an_accepted_check_runs(void)
{
    MoveGuard guard = guardWith(true, true);
    TEST_ASSERT_EQUAL(Admit::Run, guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, OK, OK, 0).action);
}

void test_park_only_refusal_waits_from_every_origin_but_the_automatic_close(void)
{
    for (Roof::Origin origin : {Roof::Origin::Person, Roof::Origin::Client, Roof::Origin::Mqtt})
    {
        MoveGuard guard = guardWith(true, true);
        const Admission a = guard.admit({Roof::Command::Open, origin, 0, AT_END}, NOT_PARKED, OK, 0);
        TEST_ASSERT_EQUAL(Admit::Wait, a.action);
        TEST_ASSERT_TRUE(guard.wait().waiting());
        TEST_ASSERT_EQUAL(Roof::Command::Open, guard.wait().command());
    }
    MoveGuard guard = guardWith(true, true);
    TEST_ASSERT_EQUAL(Admit::Refuse, guard.admit({Roof::Command::Close, Roof::Origin::Automatic, 0, AT_END}, NOT_PARKED, OK, 0).action);
}

void test_another_refusal_does_not_wait(void)
{
    MoveGuard guard = guardWith(true, true);
    const Admission a = guard.admit({Roof::Command::Open, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, WEATHER, 0);
    TEST_ASSERT_EQUAL(Admit::Refuse, a.action);
    TEST_ASSERT_EQUAL(Admit::Refuse, guard.admit({Roof::Command::Open, Roof::Origin::Client, 0, AT_END}, WEATHER, WEATHER, 0).action);
    TEST_ASSERT_FALSE(guard.wait().waiting());
}

void test_no_wait_without_a_way_to_ask(void)
{
    MoveGuard off = guardWith(true, false);
    Admission a = off.admit({Roof::Command::Close, Roof::Origin::Person, 0, AT_END}, NOT_PARKED, OK, 0);
    TEST_ASSERT_EQUAL(Admit::Refuse, a.action);
    TEST_ASSERT_EQUAL_STRING("mountNotParked", a.reason.c_str());
    TEST_ASSERT_TRUE(a.text.find("asking N.I.N.A. to park is off") != std::string::npos);
    MoveGuard none = guardWith(false, true);
    a = none.admit({Roof::Command::Close, Roof::Origin::Person, 0, AT_END}, NOT_PARKED, OK, 0);
    TEST_ASSERT_TRUE(a.text.find("no N.I.N.A. address") != std::string::npos);
}

void test_the_wait_runs_once_parked_and_asks_meanwhile(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    const Roof::Status open = inState(Roof::State::Open);
    GuardStep s = guard.step(open, false, parked(false), OK, 0);
    TEST_ASSERT_TRUE(s.requestPark);
    TEST_ASSERT_FALSE(s.run);
    guard.step(open, false, parked(true), OK, 500);
    s = guard.step(open, false, parked(true), OK, 1500);
    TEST_ASSERT_TRUE(s.run);
    TEST_ASSERT_FALSE(guard.wait().waiting());
}

void test_the_wait_gives_up_and_ends_on_another_refusal(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    TEST_ASSERT_TRUE(guard.step(inState(Roof::State::Open), false, parked(false), OK, 10000).gaveUp);
    TEST_ASSERT_TRUE(guard.wait().failed());

    guard.admit({Roof::Command::Open, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    const GuardStep s = guard.step(inState(Roof::State::Closed), false, parked(false), WEATHER, 100);
    TEST_ASSERT_TRUE(s.refused);
    TEST_ASSERT_FALSE(guard.wait().waiting());
    TEST_ASSERT_EQUAL_STRING("Weather not safe: rain", guard.wait().endText().c_str());
}

void test_a_stop_or_new_command_ends_the_wait(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    guard.stopped();
    TEST_ASSERT_FALSE(guard.wait().waiting());
    TEST_ASSERT_FALSE(guard.wait().failed());
    guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    guard.admit({Roof::Command::Open, Roof::Origin::Person, 0, AT_END}, OK, OK, 0);
    TEST_ASSERT_FALSE(guard.wait().waiting());
}

void test_open_override_takes_only_present_reasons_and_ends(void)
{
    MoveGuard guard = guardWith(true, true);
    GateInputs day = parked(true);
    day.daylightCloses = true;
    guard.ran(Roof::Command::Open, Roof::Origin::Person, OverrideDaylight | OverrideWeather, day);
    TEST_ASSERT_EQUAL_UINT8(OverrideDaylight, guard.openOverride());
    guard.step(inState(Roof::State::Opening), false, day, OK, 0);
    TEST_ASSERT_EQUAL_UINT8(OverrideDaylight, guard.openOverride());
    GateInputs rain = day;
    rain.weather = Safety::Verdict::Unsafe;
    rain.weatherReasons = Safety::ReasonRain;
    guard.step(inState(Roof::State::Open), false, rain, OK, 0);
    TEST_ASSERT_EQUAL_UINT8(0, guard.openOverride());

    guard.ran(Roof::Command::Open, Roof::Origin::Person, OverrideDaylight, day);
    guard.step(inState(Roof::State::Closed), false, day, OK, 0);
    TEST_ASSERT_EQUAL_UINT8(0, guard.openOverride());

    guard.ran(Roof::Command::Open, Roof::Origin::Person, OverrideDaylight, day);
    guard.ran(Roof::Command::Close, Roof::Origin::Person, 0, day);
    TEST_ASSERT_EQUAL_UINT8(0, guard.openOverride());
}

void test_close_park_override_lasts_for_that_close(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.ran(Roof::Command::Close, Roof::Origin::Person, OverridePark, parked(false));
    TEST_ASSERT_TRUE(guard.closeParkOverride());
    guard.step(inState(Roof::State::Closing), false, parked(false), OK, 0);
    TEST_ASSERT_TRUE(guard.closeParkOverride());
    guard.step(inState(Roof::State::Closed), false, parked(false), OK, 0);
    TEST_ASSERT_FALSE(guard.closeParkOverride());
    guard.ran(Roof::Command::Close, Roof::Origin::Person, 0, parked(false));
    TEST_ASSERT_FALSE(guard.closeParkOverride());
}

void test_override_names(void)
{
    TEST_ASSERT_EQUAL_STRING("mountNotParked", overrideName(OverridePark));
    TEST_ASSERT_EQUAL_STRING("weatherNotSafe", overrideName(OverrideWeather));
    TEST_ASSERT_EQUAL_STRING("daylight", overrideName(OverrideDaylight));
}

void test_parse_overrides(void)
{
    uint8_t bits = 9;
    std::string error;
    TEST_ASSERT_TRUE(parseOverrides("", bits, error));
    TEST_ASSERT_EQUAL_UINT8(0, bits);
    TEST_ASSERT_TRUE(parseOverrides("{}", bits, error));
    TEST_ASSERT_TRUE(parseOverrides(R"({"override":["weatherNotSafe","daylight"]})", bits, error));
    TEST_ASSERT_EQUAL_UINT8(OverrideWeather | OverrideDaylight, bits);
    TEST_ASSERT_TRUE(parseOverrides(R"({"override":["mountNotParked"]})", bits, error));
    TEST_ASSERT_EQUAL_UINT8(OverridePark, bits);
    TEST_ASSERT_FALSE(parseOverrides(R"({"override":["fault"]})", bits, error));
    TEST_ASSERT_FALSE(parseOverrides(R"({"override":"daylight"})", bits, error));
    TEST_ASSERT_FALSE(parseOverrides("nonsense", bits, error));
    TEST_ASSERT_FALSE(error.empty());
}

// Spec 027 races: the same command joins the wait; the automatic close calls
// off a waiting Open but not a waiting Close.
void test_the_same_command_joins_the_wait(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Close, Roof::Origin::Person, 0, AT_END}, NOT_PARKED, OK, 0);
    TEST_ASSERT_TRUE(guard.step(inState(Roof::State::Open), false, parked(false), OK, 0).requestPark);
    const Admission again = guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 5000);
    TEST_ASSERT_EQUAL(Admit::Wait, again.action);
    TEST_ASSERT_FALSE(guard.step(inState(Roof::State::Open), false, parked(false), OK, 5000).requestPark); // no second request
    TEST_ASSERT_EQUAL_UINT32(5000, guard.wait().leftMs(5000));                                             // the limit unchanged
    // A different command replaces it.
    guard.admit({Roof::Command::Open, Roof::Origin::Person, 0, AT_END}, NOT_PARKED, OK, 6000);
    TEST_ASSERT_EQUAL(Roof::Command::Open, guard.wait().command());
}

void test_the_automatic_close_calls_off_a_waiting_open_only(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Open, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    guard.closeWins();
    TEST_ASSERT_FALSE(guard.wait().waiting());
    guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, AT_END}, NOT_PARKED, OK, 0);
    guard.closeWins();
    TEST_ASSERT_TRUE(guard.wait().waiting());
}

// The phase: asking until a park is seen after the wait began, then holding;
// a park that breaks goes back to asking; nothing is kept over a restart.
// Spec 043 (constitution 11.1.0, II): a park is a slew, so a wait (which asks
// N.I.N.A. to park) starts only with the roof at an end. Part-open, unknown,
// moving or faulted: refused for the park, saying why, and nothing asked.
void test_no_wait_and_no_park_request_unless_the_roof_is_at_an_end(void)
{
    MoveGuardSettings settings;
    settings.ninaSet = true;
    settings.travelMm = 2000;
    for (Roof::State state : {Roof::State::Stopped, Roof::State::Unknown, Roof::State::Opening, Roof::State::Closing, Roof::State::Error})
    {
        MoveGuard guard;
        guard.configure(settings);
        Roof::Status roof = inState(state);
        roof.positionKnown = state == Roof::State::Stopped;
        roof.positionMm = 800.0f;
        for (Roof::Command command : {Roof::Command::Open, Roof::Command::Close})
        {
            const Admission a = guard.admit({command, Roof::Origin::Client, 0, roof}, NOT_PARKED, OK, 0);
            TEST_ASSERT_EQUAL(Admit::Refuse, a.action);
            TEST_ASSERT_EQUAL_STRING("mountNotParked", a.reason.c_str()); // a person may still confirm past it
            TEST_ASSERT_TRUE(a.text.find("would hit the beams") != std::string::npos);
            TEST_ASSERT_TRUE(a.text.find("N.I.N.A. was not asked to park") != std::string::npos);
            TEST_ASSERT_FALSE(guard.wait().waiting());
            TEST_ASSERT_FALSE(guard.step(roof, false, parked(false), OK, 100).requestPark);
        }
        if (state == Roof::State::Stopped)
        {
            const Admission a = guard.admit({Roof::Command::Close, Roof::Origin::Person, 0, roof}, NOT_PARKED, OK, 0);
            TEST_ASSERT_TRUE(a.text.find("Roof part-open (40 %) with the mount not parked") != std::string::npos);
        }
    }
    // At either magnet it waits and asks.
    for (Roof::State state : {Roof::State::Closed, Roof::State::Open})
    {
        MoveGuard guard;
        guard.configure(settings);
        TEST_ASSERT_EQUAL(
            Admit::Wait, guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, inState(state)}, NOT_PARKED, OK, 0).action);
        TEST_ASSERT_TRUE(guard.step(inState(state), false, parked(false), OK, 0).requestPark);
    }
}

// Spec 043: a wait whose roof leaves its end (a person's jog, a fault) ends
// at once, refused, and asks N.I.N.A. nothing more.
void test_a_wait_ends_when_the_roof_leaves_its_end(void)
{
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Close, Roof::Origin::Client, 0, inState(Roof::State::Open)}, NOT_PARKED, OK, 0);
    TEST_ASSERT_TRUE(guard.step(inState(Roof::State::Open), false, parked(false), OK, 0).requestPark);
    const GuardStep s = guard.step(inState(Roof::State::Unknown), false, parked(false), OK, 40000);
    TEST_ASSERT_TRUE(s.refused);
    TEST_ASSERT_FALSE(s.requestPark);
    TEST_ASSERT_FALSE(guard.wait().waiting());
    TEST_ASSERT_TRUE(guard.wait().failed());
    TEST_ASSERT_TRUE(guard.wait().endText().find("Roof position unknown with the mount not parked") != std::string::npos);
    TEST_ASSERT_FALSE(guard.step(inState(Roof::State::Unknown), false, parked(false), OK, 80000).requestPark);
}

void test_the_wait_phases(void)
{
    MoveGuard fresh = guardWith(true, true);
    TEST_ASSERT_EQUAL(ParkWaitPhase::Idle, fresh.wait().phase()); // a new boot: nothing waits
    MoveGuard guard = guardWith(true, true);
    guard.admit({Roof::Command::Open, Roof::Origin::Person, 0, AT_END}, NOT_PARKED, OK, 0);
    guard.step(inState(Roof::State::Closed), false, parked(false), OK, 0);
    TEST_ASSERT_EQUAL(ParkWaitPhase::Asking, guard.wait().phase());
    guard.step(inState(Roof::State::Closed), false, parked(true), OK, 100);
    TEST_ASSERT_EQUAL(ParkWaitPhase::Holding, guard.wait().phase());
    guard.step(inState(Roof::State::Closed), false, parked(false), OK, 200);
    TEST_ASSERT_EQUAL(ParkWaitPhase::Asking, guard.wait().phase());
    // N.I.N.A. gone: not parked to the limit, then the alert.
    TEST_ASSERT_TRUE(guard.step(inState(Roof::State::Closed), false, parked(false), OK, 10000).gaveUp);
    TEST_ASSERT_EQUAL(ParkWaitPhase::Idle, guard.wait().phase());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_accepted_check_runs);
    RUN_TEST(test_park_only_refusal_waits_from_every_origin_but_the_automatic_close);
    RUN_TEST(test_another_refusal_does_not_wait);
    RUN_TEST(test_no_wait_without_a_way_to_ask);
    RUN_TEST(test_the_wait_runs_once_parked_and_asks_meanwhile);
    RUN_TEST(test_the_wait_gives_up_and_ends_on_another_refusal);
    RUN_TEST(test_a_stop_or_new_command_ends_the_wait);
    RUN_TEST(test_open_override_takes_only_present_reasons_and_ends);
    RUN_TEST(test_close_park_override_lasts_for_that_close);
    RUN_TEST(test_override_names);
    RUN_TEST(test_parse_overrides);
    RUN_TEST(test_the_same_command_joins_the_wait);
    RUN_TEST(test_the_automatic_close_calls_off_a_waiting_open_only);
    RUN_TEST(test_the_wait_phases);
    RUN_TEST(test_no_wait_and_no_park_request_unless_the_roof_is_at_an_end);
    RUN_TEST(test_a_wait_ends_when_the_roof_leaves_its_end);
    return UNITY_END();
}
