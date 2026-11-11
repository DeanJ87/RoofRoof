// Spec 014: the policy settings outside the close sequence. The remote stop
// rule (spec 006 item 2 as a setting, D-45): refused by default, and with the
// setting a remote stop of a close in unsafe weather counts as a person's, so
// the stand-down's critical alert always follows. The MQTT at-rest change gap
// and the daylight band at both ends of their ranges.

#include <cmath>
#include <unity.h>

#include "../close_rig/CloseRig.h"
#include "../roof_sim/RoofSim.h"
#include "CommandCheck.h"
#include "Daylight.h"
#include "DomeBridge.h"
#include "RoofMqtt.h"
#include "SettingsDeps.h"
#include "SunPosition.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

// --- The remote stop rule (spec 006 item 2 as a setting; D-45) ---

namespace
{
    // An Alpaca or MQTT stop of a close in unsafe weather, with the rule as given.
    RoofIo::CommandCheck remoteStopOfAClose(
        Roof::Origin origin, bool remoteStopsClose, Safety::Verdict weather, Roof::Command command = Roof::Command::Stop)
    {
        RoofTest::Bench bench(Roof::Settings(), 900.0f);
        bench.command(Roof::Command::Close, Roof::Origin::Person);
        bench.steps(25);
        TEST_ASSERT_EQUAL(State::Closing, bench.state());
        RoofIo::GateInputs gates;
        gates.weather = weather;
        gates.weatherText = "unsafe: rain";
        gates.parked = true;
        RoofIo::CommandContext context;
        context.remoteStopsClose = remoteStopsClose;
        return RoofIo::checkCommand(command, origin, gates, bench.roof, context);
    }
} // namespace

void test_remote_stop_of_a_close_refused_by_default(void)
{
    for (Roof::Origin o : {Roof::Origin::Client, Roof::Origin::Mqtt})
    {
        const RoofIo::CommandCheck check = remoteStopOfAClose(o, false, Safety::Verdict::Unsafe);
        TEST_ASSERT_FALSE(check.accepted);
        TEST_ASSERT_EQUAL_STRING("unsafeClose", check.reason.c_str());
        TEST_ASSERT_FALSE(check.standsDown);
    }
}

void test_remote_stop_with_the_setting_counts_as_a_persons(void)
{
    for (Roof::Origin o : {Roof::Origin::Client, Roof::Origin::Mqtt})
    {
        const RoofIo::CommandCheck check = remoteStopOfAClose(o, true, Safety::Verdict::Unsafe);
        TEST_ASSERT_TRUE(check.accepted);
        TEST_ASSERT_TRUE(check.standsDown);
        // In safe weather a remote stop is the ordinary one: accepted, never a person's.
        const RoofIo::CommandCheck safe = remoteStopOfAClose(o, true, Safety::Verdict::Safe);
        TEST_ASSERT_TRUE(safe.accepted);
        TEST_ASSERT_FALSE(safe.standsDown);
        TEST_ASSERT_FALSE(RoofIo::countsAsPersonStop(Roof::Command::Stop, o));
        // Only a Stop: a remote abort (which would make the position unknown) stays refused.
        const RoofIo::CommandCheck abort = remoteStopOfAClose(o, true, Safety::Verdict::Unsafe, Roof::Command::Abort);
        TEST_ASSERT_FALSE(abort.accepted);
        TEST_ASSERT_FALSE(abort.standsDown);
    }
    // A person's and the controller's stops never use the flag.
    TEST_ASSERT_FALSE(remoteStopOfAClose(Roof::Origin::Person, true, Safety::Verdict::Unsafe).standsDown);
    TEST_ASSERT_FALSE(remoteStopOfAClose(Roof::Origin::Automatic, true, Safety::Verdict::Unsafe).standsDown);
}

// The honoured remote stop, counted as a person's, stands the sequence down
// with its critical alert: never an open roof in the rain without it.
void test_honoured_remote_stop_raises_the_critical_alert(void)
{
    Rig r;
    r.startAndClose();
    r.in.personStops = 1; // RoofControl counts the honoured stop
    r.in.roof = State::Stopped;
    const Decision d = r.at(T0 + 10000);
    assertEvent(d, Event::StoppedByPerson, Cause::None);
    TEST_ASSERT_TRUE(d.alert);
    TEST_ASSERT_TRUE(isCritical(d.event));
}

// D-45: the remote stop needs Alpaca on or MQTT roof commands.
void test_remote_stop_dependency_D45(void)
{
    Config cfg = Config::createDefault();
    cfg.autoClose.remoteStop = true;
    Deps::Facts facts;
    TEST_ASSERT_FALSE(Deps::isActive(Deps::evaluate(cfg, facts), "autoClose.remoteStop"));
    cfg.alpaca.enabled = true;
    TEST_ASSERT_TRUE(Deps::isActive(Deps::evaluate(cfg, facts), "autoClose.remoteStop"));
    cfg.alpaca.enabled = false;
    cfg.mqtt.enabled = true;
    cfg.mqtt.broker = "broker.local";
    cfg.mqtt.roofCommands = true;
    TEST_ASSERT_TRUE(Deps::isActive(Deps::evaluate(cfg, facts), "autoClose.remoteStop"));
}

// --- MQTT at-rest change gap (spec 008 item 10) ---

void test_mqtt_change_gap_limits(void)
{
    RoofIo::Snapshot s;
    s.status.state = State::Closed;
    s.status.positionKnown = true;
    s.weather = Safety::Verdict::Safe;
    RoofIo::Snapshot changed = s;
    changed.status.state = State::Open;
    const RoofIo::Mqtt::StateKey before = RoofIo::Mqtt::keyOf(s);
    const RoofIo::Mqtt::StateKey after = RoofIo::Mqtt::keyOf(changed);
    for (const uint32_t gap : {1000u, 60000u})
    {
        RoofIo::Mqtt::StateSchedule schedule;
        schedule.setChangeMinMs(gap);
        schedule.published(before, 1, 1000, true);
        TEST_ASSERT_FALSE(schedule.due(after, 1, 1000 + gap - 1, 3600000));
        TEST_ASSERT_TRUE(schedule.due(after, 1, 1000 + gap, 3600000));
    }
    // Never later than the publish interval.
    RoofIo::Mqtt::StateSchedule shorter;
    shorter.setChangeMinMs(60000);
    shorter.published(before, 1, 1000, true);
    TEST_ASSERT_FALSE(shorter.due(after, 1, 5999, 5000));
    TEST_ASSERT_TRUE(shorter.due(after, 1, 6000, 5000));
    RoofIo::Mqtt::StateSchedule clamped;
    clamped.setChangeMinMs(0);
    clamped.published(before, 1, 1000, true);
    TEST_ASSERT_FALSE(clamped.due(after, 1, 1999, 3600000));
    clamped.setChangeMinMs(10000000);
    TEST_ASSERT_TRUE(clamped.due(after, 1, 61000, 3600000));
}

// --- Daylight band (spec 013 item 2) ---

namespace
{
    constexpr double LAT = 51.48;
    constexpr double LON = 0.0;
    constexpr int64_t NOON = 1718971320;
    constexpr int64_t MIDNIGHT = 1718928000;

    int64_t risingThrough(double target)
    {
        for (int64_t t = MIDNIGHT; t < NOON; ++t)
            if (Astro::sunElevationDeg(t, LAT, LON) >= target)
                return t;
        return NOON;
    }

    Daylight::Settings site(float band)
    {
        Daylight::Settings s;
        s.mode = Daylight::Mode::Close;
        s.limitDeg = 2.0f;
        s.locationSet = true;
        s.latitude = LAT;
        s.longitude = LON;
        s.bandDeg = band;
        return s;
    }
} // namespace

void test_daylight_band_limits(void)
{
    for (const float band : {0.1f, 2.0f})
    {
        Daylight::Watch w;
        TEST_ASSERT_EQUAL(Daylight::State::Day, w.update(site(band), true, risingThrough(2.0)).state);
        const int64_t edge = risingThrough(2.0 - static_cast<double>(band));
        TEST_ASSERT_EQUAL(Daylight::State::Day, w.update(site(band), true, edge).state);
        TEST_ASSERT_EQUAL(Daylight::State::Night, w.update(site(band), true, edge - 1).state);
    }
    Daylight::Watch w;
    TEST_ASSERT_EQUAL_FLOAT(0.1f, w.update(site(0.0f), true, NOON).bandDeg); // never 0: the state would flicker
    TEST_ASSERT_EQUAL_FLOAT(2.0f, w.update(site(9.0f), true, NOON).bandDeg);
    Config cfg = Config::createDefault();
    TEST_ASSERT_EQUAL_FLOAT(0.5f, Daylight::settingsFrom(cfg).bandDeg);
    cfg.daylight.bandDeg = 0.09f;
    TEST_ASSERT_FALSE(cfg.validate());
    cfg.daylight.bandDeg = 2.01f;
    TEST_ASSERT_FALSE(cfg.validate());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_remote_stop_of_a_close_refused_by_default);
    RUN_TEST(test_remote_stop_with_the_setting_counts_as_a_persons);
    RUN_TEST(test_honoured_remote_stop_raises_the_critical_alert);
    RUN_TEST(test_remote_stop_dependency_D45);
    RUN_TEST(test_mqtt_change_gap_limits);
    RUN_TEST(test_daylight_band_limits);
    return UNITY_END();
}
