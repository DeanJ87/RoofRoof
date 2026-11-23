// Spec 022: the test box's bypasses (constitution 3.0.0, Principles II and
// III). The settings (defaults, JSON, validation, the deadline the save
// stamps), when a bypass is in effect, what it does to the weather verdict,
// the park result and the hold, the gates every command passes, the daylight
// close, the texts, the status object, the alert and the Dome's description.
// And that no fault, the supply or the Hall rail can be bypassed.

#include <string>
#include <unity.h>

#include "../roof_sim/RoofSim.h"
#include "Config.h"
#include "ConfigSave.h"
#include "Daylight.h"
#include "ParkRule.h"
#include "RoofGates.h"
#include "SafetyInput.h"
#include "TestBox.h"

using namespace SQM;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr int64_t NOW = 1764000000; // 2025-11-24 16:00 UTC
    constexpr uint32_t HOUR = 3600;

    TestBoxConfig allOn(uint32_t until = NOW + 72 * HOUR)
    {
        TestBoxConfig t;
        t.park = t.weather = t.daylight = true;
        t.until = until;
        return t;
    }

    TestBox::State inEffect(const TestBoxConfig &t = allOn(), TestBox::Sources sources = TestBox::Sources{})
    {
        return TestBox::evaluate(t, sources, true, NOW);
    }

    // Both SQMeter sources off and no rain contact: "no weather source".
    Safety::WeatherVerdict noSource(Safety::RainContact rain = Safety::RainContact::NotFitted)
    {
        Safety::WeatherInputs in;
        in.rainContact = rain;
        in.nowMs = 100000;
        return Safety::evaluateWeather(in);
    }

    // N.I.N.A. not configured; `fitted` switches reading `reading`.
    Safety::ParkResult noNina(uint8_t fitted = 0, Safety::SwitchReading reading = Safety::SwitchReading::Closed)
    {
        Safety::ParkInputs in;
        in.nina.status = Nina::ReadingStatus::NotConfigured;
        in.fittedSwitches = fitted;
        in.switch1 = in.switch2 = reading;
        in.nowMs = 100000;
        return Safety::evaluatePark(in);
    }

    Config withNina()
    {
        Config c = Config::createDefault();
        c.park.ninaUrl = "http://192.168.1.20:1888";
        return c;
    }

    std::string saveError(const std::string &json, const Config &base = Config::createDefault())
    {
        std::string error;
        const auto merged = Config::fromJson(json, &base, &error);
        TEST_ASSERT_FALSE_MESSAGE(merged.has_value(), json.c_str());
        return error;
    }
} // namespace

// --- Settings -------------------------------------------------------------------

void test_every_bypass_is_off_by_default_and_after_an_upgrade(void)
{
    const Config c = Config::createDefault();
    TEST_ASSERT_FALSE(c.testBox.any());
    TEST_ASSERT_EQUAL_UINT32(72, c.testBox.hours);
    TEST_ASSERT_EQUAL_UINT32(0, c.testBox.until);
    // Settings stored before spec 022 have no testBox.
    Config stored = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson(R"({"deviceName":"Old"})", stored, false));
    TEST_ASSERT_FALSE(stored.testBox.any());
    TEST_ASSERT_EQUAL_UINT8(3, stored.alerts.testBox.level);
}

void test_settings_round_trip_and_the_deadline_only_from_stored_settings(void)
{
    Config c = Config::createDefault();
    c.testBox = allOn(1234567);
    c.testBox.hours = 5;
    Config back = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson(c.toJson(), back, false)); // stored: the deadline comes back
    TEST_ASSERT_TRUE(back.testBox.park && back.testBox.weather && back.testBox.daylight);
    TEST_ASSERT_EQUAL_UINT32(5, back.testBox.hours);
    TEST_ASSERT_EQUAL_UINT32(1234567, back.testBox.until);
    // A request (or a restore) never sets it.
    const auto merged = Config::fromJson(R"({"testBox":{"daylight":true,"until":4000000000}})", &c);
    TEST_ASSERT_TRUE(merged.has_value());
    TEST_ASSERT_EQUAL_UINT32(1234567, merged->testBox.until);
}

void test_hours_between_1_and_720(void)
{
    TEST_ASSERT_EQUAL_STRING("Test box: hours must be between 1 and 720", saveError(R"({"testBox":{"hours":0}})").c_str());
    saveError(R"({"testBox":{"hours":721}})");
    for (const char *ok : {R"({"testBox":{"hours":1}})", R"({"testBox":{"hours":720}})"})
        TEST_ASSERT_TRUE(Config::fromJson(ok).has_value());
}

void test_the_park_bypass_never_beside_a_nina_address(void)
{
    const std::string e1 = saveError(R"({"testBox":{"park":true}})", withNina());
    TEST_ASSERT_EQUAL_STRING(
        "Test box: the park bypass is for a box with no telescope; clear the N.I.N.A. address first (or turn the bypass off)", e1.c_str());
    Config bypassed = Config::createDefault();
    bypassed.testBox.park = true;
    saveError(R"({"park":{"nina":{"url":"http://10.0.0.2:1888"}}})", bypassed);
    // Clearing the address in the same save is fine.
    TEST_ASSERT_TRUE(Config::fromJson(R"({"testBox":{"park":true},"park":{"nina":{"url":""}}})", nullptr).has_value());
}

void test_the_weather_bypass_never_beside_an_sqmeter_source(void)
{
    Config bypassed = Config::createDefault();
    bypassed.testBox.weather = true;
    const std::string e = saveError(R"({"safety":{"http":{"enabled":true,"url":"http://10.0.0.3/api/safety"}}})", bypassed);
    TEST_ASSERT_EQUAL_STRING(
        "Test box: the weather bypass is for a box with no weather source; turn SQMeter HTTP and MQTT off first (or turn the bypass off)",
        e.c_str());
    saveError(R"({"safety":{"mqtt":{"enabled":true}}})", bypassed);
    // A rain contact is a reading, not a source it stands in for.
    TEST_ASSERT_TRUE(Config::fromJson(R"({"safety":{"rainContact":true}})", &bypassed).has_value());
}

void test_stored_settings_with_both_still_load(void)
{
    Config c = Config::createDefault();
    TEST_ASSERT_TRUE(Config::applyJson(R"({"park":{"nina":{"url":"http://10.0.0.2:1888"}},"testBox":{"park":true}})", c, false));
    TEST_ASSERT_TRUE(c.testBox.park);
}

void test_the_test_box_alert_is_never_off(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "Alerts: the test box alert can't be off", saveError(R"({"alerts":{"events":{"test_box":{"level":0}}}})").c_str());
    TEST_ASSERT_TRUE(Config::fromJson(R"({"alerts":{"events":{"test_box":{"level":1}}}})").has_value());
    AlertsConfig a = Config::createDefault().alerts;
    a.testBox.level = 0;
    a.raiseCriticalToMinimum(); // stored settings: raised, never refused
    TEST_ASSERT_EQUAL_UINT8(1, a.testBox.level);
}

// Bench regression: settings stored by spec 021 (no testBox, no park.requestPark),
// then a no-op save merged into a copy of them as the firmware's save does
// (Config::applyJson into `next`, then the stamp). It saves, and nothing turns on.
void test_a_save_over_settings_stored_before_spec_022(void)
{
    Config stored = Config::createDefault();
    const std::string before =
        R"({"deviceName":"Bench","ota":{"enabled":true},"park":{"nina":{"url":"","api":"v2"},"switches":"none","timeoutSeconds":240},)"
        R"("safety":{"http":{"enabled":false,"url":""},"mqtt":{"enabled":false,"topic":"sqmeter"},"staleSeconds":30,"rainContact":false}})";
    TEST_ASSERT_TRUE(Config::applyJson(before, stored, false));
    Config next = stored;
    std::string reason;
    const std::string body = R"({"ota":{"enabled":false}})";
    TEST_ASSERT_TRUE_MESSAGE(Config::applyJson(body, next, true, &reason), reason.c_str());
    TEST_ASSERT_TRUE(ConfigSave::stampTestBox(
        stored.testBox, next.testBox, ConfigSave::sendsTestBoxHours(body), ConfigSave::Clock{true, static_cast<uint32_t>(NOW)}, &reason));
    TEST_ASSERT_FALSE(next.ota.enabled);
    TEST_ASSERT_FALSE(next.testBox.any());
    TEST_ASSERT_EQUAL_UINT32(0, next.testBox.until);
    TEST_ASSERT_TRUE(next.park.requestPark);
    TEST_ASSERT_TRUE(next.validate(&reason));
}

// --- The deadline (the save's stamp) ---------------------------------------------

void test_turning_a_bypass_on_starts_the_deadline(void)
{
    TestBoxConfig live;
    TestBoxConfig next = live;
    next.park = true;
    next.hours = 10;
    TEST_ASSERT_TRUE(ConfigSave::stampTestBox(live, next, false, ConfigSave::Clock{true, static_cast<uint32_t>(NOW)}, nullptr));
    TEST_ASSERT_EQUAL_UINT32(NOW + 10 * HOUR, next.until);
}

void test_the_deadline_needs_the_clock(void)
{
    TestBoxConfig live;
    TestBoxConfig next = live;
    next.weather = true;
    std::string error;
    TEST_ASSERT_FALSE(ConfigSave::stampTestBox(live, next, false, ConfigSave::Clock{false, static_cast<uint32_t>(0)}, &error));
    TEST_ASSERT_EQUAL_STRING("Test box: the clock isn't set yet (Settings → Time); a bypass needs a deadline", error.c_str());
}

void test_an_unrelated_save_or_turning_one_off_keeps_the_deadline(void)
{
    const TestBoxConfig live = allOn(NOW + 5);
    TestBoxConfig next = live;
    TEST_ASSERT_TRUE(
        ConfigSave::stampTestBox(live, next, false, ConfigSave::Clock{false, static_cast<uint32_t>(0)}, nullptr)); // no clock needed
    TEST_ASSERT_EQUAL_UINT32(NOW + 5, next.until);
    next.until = 99; // the merge brought no deadline: the live one wins
    next.daylight = false;
    TEST_ASSERT_TRUE(ConfigSave::stampTestBox(live, next, false, ConfigSave::Clock{true, static_cast<uint32_t>(NOW + 100)}, nullptr));
    TEST_ASSERT_EQUAL_UINT32(NOW + 5, next.until);
}

void test_sending_hours_renews(void)
{
    const TestBoxConfig live = allOn(NOW - 10); // expired
    TestBoxConfig next = live;
    next.hours = 2;
    TEST_ASSERT_TRUE(ConfigSave::stampTestBox(live, next, true, ConfigSave::Clock{true, static_cast<uint32_t>(NOW)}, nullptr));
    TEST_ASSERT_EQUAL_UINT32(NOW + 2 * HOUR, next.until);
    TEST_ASSERT_TRUE(ConfigSave::sendsTestBoxHours(R"({"testBox":{"hours":72}})"));
    TEST_ASSERT_FALSE(ConfigSave::sendsTestBoxHours(R"({"testBox":{"park":false}})"));
    TEST_ASSERT_FALSE(ConfigSave::sendsTestBoxHours(R"({"roof":{"hours":3}})"));
    TEST_ASSERT_FALSE(ConfigSave::sendsTestBoxHours("not json"));
}

void test_turning_every_bypass_off_clears_the_deadline(void)
{
    const TestBoxConfig live = allOn();
    TestBoxConfig next;
    next.until = live.until;
    TEST_ASSERT_TRUE(ConfigSave::stampTestBox(live, next, true, ConfigSave::Clock{false, static_cast<uint32_t>(0)}, nullptr));
    TEST_ASSERT_EQUAL_UINT32(0, next.until);
}

// --- In effect ----------------------------------------------------------------------

void test_in_effect_only_before_the_deadline_and_with_the_clock(void)
{
    const TestBoxConfig t = allOn(NOW + 1);
    TestBox::State s = TestBox::evaluate(t, {}, true, NOW);
    TEST_ASSERT_TRUE(s.park && s.weather && s.daylight && s.active());
    TEST_ASSERT_EQUAL_UINT8(TestBox::BitPark | TestBox::BitWeather | TestBox::BitDaylight, s.bits());
    s = TestBox::evaluate(t, {}, true, NOW + 1); // at the deadline: over
    TEST_ASSERT_FALSE(s.active());
    TEST_ASSERT_TRUE(s.expired && s.set);
    s = TestBox::evaluate(t, {}, false, 0);
    TEST_ASSERT_FALSE(s.active());
    TEST_ASSERT_TRUE(s.waitingForClock);
    s = TestBox::evaluate(allOn(0), {}, true, NOW); // a restored backup: no deadline
    TEST_ASSERT_FALSE(s.active());
    TEST_ASSERT_TRUE(s.expired);
    s = TestBox::evaluate(TestBoxConfig{}, {}, true, NOW);
    TEST_ASSERT_FALSE(s.set || s.expired || s.waitingForClock);
}

void test_a_bypass_beside_its_source_has_no_effect(void)
{
    TestBox::State s = inEffect(allOn(), TestBox::Sources{true, false});
    TEST_ASSERT_FALSE(s.park);
    TEST_ASSERT_TRUE(s.weather);
    s = inEffect(allOn(), TestBox::Sources{false, true});
    TEST_ASSERT_TRUE(s.park);
    TEST_ASSERT_FALSE(s.weather);
    Config c = withNina();
    c.safety.mqttEnabled = true;
    const TestBox::Sources sources = TestBox::sourcesOf(c.safety, c.park);
    TEST_ASSERT_TRUE(sources.ninaConfigured && sources.sqmeterOn);
}

// --- The weather verdict ---------------------------------------------------------------

void test_the_weather_bypass_stands_in_for_a_missing_source(void)
{
    Safety::WeatherVerdict v = noSource();
    TEST_ASSERT_TRUE(v.verdict == Safety::Verdict::Unsafe);
    TestBox::applyWeather(v, inEffect());
    TEST_ASSERT_TRUE(v.safe());
    TEST_ASSERT_TRUE(v.bypassed);
    TEST_ASSERT_EQUAL_STRING("bypassed (test box): no weather source", Safety::describe(v).c_str());
    // A dry rain contact is fine too.
    v = noSource(Safety::RainContact::Dry);
    TestBox::applyWeather(v, inEffect());
    TEST_ASSERT_TRUE(v.safe());
}

void test_a_wet_or_unreadable_rain_contact_still_counts(void)
{
    for (Safety::RainContact rain : {Safety::RainContact::Raining, Safety::RainContact::Unreadable})
    {
        Safety::WeatherVerdict v = noSource(rain);
        TestBox::applyWeather(v, inEffect());
        TEST_ASSERT_TRUE(v.verdict == Safety::Verdict::Unsafe);
        TEST_ASSERT_FALSE(v.bypassed);
    }
}

void test_a_configured_sources_doubt_is_never_bypassed(void)
{
    Safety::WeatherInputs in;
    in.http.enabled = true;
    in.nowMs = 100000; // past the start-up window: stale
    Safety::WeatherVerdict v = Safety::evaluateWeather(in);
    TestBox::applyWeather(v, inEffect());
    TEST_ASSERT_TRUE(v.verdict == Safety::Verdict::Unsafe);
    TEST_ASSERT_FALSE(v.bypassed);
}

void test_the_weather_bypass_out_of_effect_changes_nothing(void)
{
    Safety::WeatherVerdict v = noSource();
    TestBox::applyWeather(v, TestBox::evaluate(allOn(NOW), {}, true, NOW));
    TEST_ASSERT_TRUE(v.verdict == Safety::Verdict::Unsafe);
}

// --- The park result and the hold ------------------------------------------------------

void test_the_park_bypass_stands_in_for_nina(void)
{
    Safety::ParkResult r = noNina();
    TEST_ASSERT_FALSE(r.parked);
    Safety::CloseHold hold;
    hold.freshUntilMs = 5;
    TestBox::applyPark(r, hold, inEffect());
    TEST_ASSERT_TRUE(r.parked && r.bypassed);
    TEST_ASSERT_EQUAL_UINT32(0, r.reasons);
    TEST_ASSERT_TRUE(hold.parked);
    TEST_ASSERT_FALSE(hold.riding);
    TEST_ASSERT_EQUAL_UINT64(0, hold.freshUntilMs);
    TEST_ASSERT_EQUAL_STRING("bypassed (test box): no telescope under this roof", Safety::describe(r).c_str());
    // With closed switches fitted too.
    r = noNina(2, Safety::SwitchReading::Closed);
    TestBox::applyPark(r, hold, inEffect());
    TEST_ASSERT_TRUE(r.parked);
}

void test_an_open_or_unreadable_switch_still_means_not_parked(void)
{
    for (Safety::SwitchReading reading : {Safety::SwitchReading::Open, Safety::SwitchReading::Unreadable})
    {
        Safety::ParkResult r = noNina(1, reading);
        Safety::CloseHold hold;
        TestBox::applyPark(r, hold, inEffect());
        TEST_ASSERT_FALSE(r.parked);
        TEST_ASSERT_FALSE(r.bypassed);
        TEST_ASSERT_FALSE(hold.parked);
        TEST_ASSERT_EQUAL_UINT32(0, r.reasons & Safety::ParkNinaNotConfigured); // the switch is named, not N.I.N.A.
        TEST_ASSERT_NOT_EQUAL(0, r.reasons);
    }
}

void test_a_real_nina_reading_is_never_set_aside(void)
{
    Safety::ParkInputs in;
    in.nina.status = Nina::ReadingStatus::Good;
    in.nina.connected = true;
    in.nina.atPark = false;
    in.nina.takenAtMs = 99000;
    in.nowMs = 100000;
    Safety::ParkResult r = Safety::evaluatePark(in);
    Safety::CloseHold hold;
    // Even a state that says park (only possible with stored settings written by hand).
    TestBox::applyPark(r, hold, inEffect());
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_FALSE(r.bypassed);
}

// --- The gates every command passes --------------------------------------------------

namespace
{
    RoofIo::GateInputs gatesFrom(const Safety::WeatherVerdict &v, const Safety::ParkResult &p, const Safety::CloseHold &hold)
    {
        RoofIo::GateInputs g;
        g.weather = v.verdict;
        g.weatherText = Safety::describe(v);
        g.parked = p.parked;
        g.parkText = Safety::describe(p);
        g.ninaGrace = hold.riding;
        g.parkFreshUntilMs = hold.freshUntilMs;
        g.nowMs = 100000;
        return g;
    }

    RoofIo::GateInputs bypassedGates(const TestBox::State &state)
    {
        Safety::WeatherVerdict v = noSource();
        Safety::ParkResult p = noNina();
        Safety::CloseHold hold;
        TestBox::applyWeather(v, state);
        TestBox::applyPark(p, hold, state);
        return gatesFrom(v, p, hold);
    }
} // namespace

void test_open_close_and_moves_pass_with_the_bypasses(void)
{
    const RoofIo::GateInputs g = bypassedGates(inEffect());
    TEST_ASSERT_TRUE(RoofIo::gateCommand(Roof::Command::Open, g) == RoofIo::GateRefusal::None);
    TEST_ASSERT_TRUE(RoofIo::gateCommand(Roof::Command::Close, g) == RoofIo::GateRefusal::None);
    TEST_ASSERT_TRUE(RoofIo::closeMayContinue(g));
    TEST_ASSERT_EQUAL_UINT32(0, RoofIo::interlockBits(g));
}

void test_without_them_todays_refusals(void)
{
    const RoofIo::GateInputs g = bypassedGates(TestBox::evaluate(TestBoxConfig{}, {}, true, NOW));
    TEST_ASSERT_TRUE(RoofIo::gateCommand(Roof::Command::Open, g) == RoofIo::GateRefusal::WeatherNotSafe);
    TEST_ASSERT_TRUE(RoofIo::gateCommand(Roof::Command::Close, g) == RoofIo::GateRefusal::MountNotParked);
    TEST_ASSERT_FALSE(RoofIo::closeMayContinue(g));
}

void test_a_switch_that_opens_during_a_bypassed_close_stops_it(void)
{
    RoofIo::GateInputs g = bypassedGates(inEffect());
    g.fittedSwitches = 1;
    g.switch1 = RoofIo::Contact::Closed;
    TEST_ASSERT_TRUE(RoofIo::closeMayContinue(g));
    g.switch1 = RoofIo::Contact::Open;
    TEST_ASSERT_FALSE(RoofIo::closeMayContinue(g));
    TEST_ASSERT_EQUAL_UINT32(RoofIo::InterlockSwitch1, RoofIo::interlockBits(g));
}

void test_the_bypass_ending_during_a_close_stops_it(void)
{
    const RoofIo::GateInputs ended = bypassedGates(TestBox::evaluate(allOn(NOW), {}, true, NOW));
    TEST_ASSERT_FALSE(RoofIo::closeMayContinue(ended));
}

void test_a_stalled_hand_over_still_counts_as_not_parked(void)
{
    const RoofIo::GateInputs g = RoofIo::currentGates(bypassedGates(inEffect()), true, RoofIo::SAFETY_HANDOVER_STALE_MS + 1);
    TEST_ASSERT_FALSE(g.parked);
    TEST_ASSERT_TRUE(g.weather != Safety::Verdict::Safe);
}

// --- Asking N.I.N.A. to park ---------------------------------------------------------

void test_asking_nina_to_park_is_a_setting_on_by_default(void)
{
    Config c = Config::createDefault();
    TEST_ASSERT_TRUE(c.park.requestPark);
    TEST_ASSERT_TRUE(Config::applyJson(R"({"deviceName":"Old"})", c, false)); // an upgrade keeps it on
    TEST_ASSERT_TRUE(c.park.requestPark);
    const auto off = Config::fromJson(R"({"park":{"requestPark":false}})", &c);
    TEST_ASSERT_TRUE(off.has_value());
    TEST_ASSERT_FALSE(off->park.requestPark);
    // Shown with the bypasses: only when an address is set and asking is on.
    Config nina = withNina();
    TEST_ASSERT_TRUE(inEffect(allOn(), TestBox::sourcesOf(nina.safety, nina.park)).parkRequests);
    nina.park.requestPark = false;
    TEST_ASSERT_FALSE(inEffect(allOn(), TestBox::sourcesOf(nina.safety, nina.park)).parkRequests);
    TEST_ASSERT_FALSE(inEffect(allOn(), TestBox::sourcesOf(c.safety, c.park)).parkRequests); // no address
}

// --- No fault can be bypassed ----------------------------------------------------------

void test_faults_still_latch_once_the_bypassed_gates_let_a_move_start(void)
{
    using RoofTest::Bench;
    const RoofIo::GateInputs g = bypassedGates(inEffect());
    TEST_ASSERT_TRUE(RoofIo::gateCommand(Roof::Command::Open, g) == RoofIo::GateRefusal::None);
    // The Hall sensor at a rail, the supply below its minimum, a jam (timeout):
    // the motion logic never sees the bypass.
    {
        Bench b;
        TEST_ASSERT_TRUE(b.command(Roof::Command::Open).accepted);
        b.steps(100);
        b.sim.railCounts = 4095;
        b.steps(50);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == Roof::FaultKind::HallRail);
    }
    {
        Bench b;
        TEST_ASSERT_TRUE(b.command(Roof::Command::Open).accepted);
        b.steps(100);
        b.sim.supplyVolts = 5.5f; // a board on USB
        b.steps(50);
        TEST_ASSERT_TRUE(b.roof.status().fault.kind == Roof::FaultKind::SupplyLow);
    }
    {
        Bench b;
        b.sim.jammed = true;
        TEST_ASSERT_TRUE(b.command(Roof::Command::Open).accepted);
        b.runToRest();
        TEST_ASSERT_TRUE(b.roof.status().fault.kind != Roof::FaultKind::None);
    }
}

// --- Daylight --------------------------------------------------------------------------

void test_the_daylight_bypass_keeps_the_state_but_never_closes(void)
{
    Daylight::Settings s;
    s.mode = Daylight::Mode::Close;
    Daylight::Watch watch;
    Daylight::View v = watch.update(s, true, NOW, true); // the sun forced up
    TEST_ASSERT_TRUE(v.closes());
    s.testBox = true;
    v = watch.update(s, true, NOW, true);
    TEST_ASSERT_TRUE(v.state == Daylight::State::Day);
    TEST_ASSERT_FALSE(v.closes());
    TEST_ASSERT_TRUE(v.bypassed);
    TEST_ASSERT_EQUAL_STRING("Sun 10.0° ≥ 0.0° (close bypassed: test box)", Daylight::describe(v).c_str());
    s.mode = Daylight::Mode::Alert; // nothing to bypass: the text is unchanged
    v = watch.update(s, true, NOW, true);
    TEST_ASSERT_EQUAL_STRING("Sun 10.0° ≥ 0.0°", Daylight::describe(v).c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_bypass_is_off_by_default_and_after_an_upgrade);
    RUN_TEST(test_settings_round_trip_and_the_deadline_only_from_stored_settings);
    RUN_TEST(test_hours_between_1_and_720);
    RUN_TEST(test_the_park_bypass_never_beside_a_nina_address);
    RUN_TEST(test_the_weather_bypass_never_beside_an_sqmeter_source);
    RUN_TEST(test_stored_settings_with_both_still_load);
    RUN_TEST(test_the_test_box_alert_is_never_off);
    RUN_TEST(test_a_save_over_settings_stored_before_spec_022);
    RUN_TEST(test_turning_a_bypass_on_starts_the_deadline);
    RUN_TEST(test_the_deadline_needs_the_clock);
    RUN_TEST(test_an_unrelated_save_or_turning_one_off_keeps_the_deadline);
    RUN_TEST(test_sending_hours_renews);
    RUN_TEST(test_turning_every_bypass_off_clears_the_deadline);
    RUN_TEST(test_in_effect_only_before_the_deadline_and_with_the_clock);
    RUN_TEST(test_a_bypass_beside_its_source_has_no_effect);
    RUN_TEST(test_the_weather_bypass_stands_in_for_a_missing_source);
    RUN_TEST(test_a_wet_or_unreadable_rain_contact_still_counts);
    RUN_TEST(test_a_configured_sources_doubt_is_never_bypassed);
    RUN_TEST(test_the_weather_bypass_out_of_effect_changes_nothing);
    RUN_TEST(test_the_park_bypass_stands_in_for_nina);
    RUN_TEST(test_an_open_or_unreadable_switch_still_means_not_parked);
    RUN_TEST(test_a_real_nina_reading_is_never_set_aside);
    RUN_TEST(test_open_close_and_moves_pass_with_the_bypasses);
    RUN_TEST(test_without_them_todays_refusals);
    RUN_TEST(test_a_switch_that_opens_during_a_bypassed_close_stops_it);
    RUN_TEST(test_the_bypass_ending_during_a_close_stops_it);
    RUN_TEST(test_a_stalled_hand_over_still_counts_as_not_parked);
    RUN_TEST(test_asking_nina_to_park_is_a_setting_on_by_default);
    RUN_TEST(test_faults_still_latch_once_the_bypassed_gates_let_a_move_start);
    RUN_TEST(test_the_daylight_bypass_keeps_the_state_but_never_closes);
    return UNITY_END();
}
