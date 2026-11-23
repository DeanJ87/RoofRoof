// Spec 024 (constitution 5.0.0, Principle II): the park source. Settings
// (default, upgrade from park.switches, the combinations a save may make),
// the park rule with the switches as the only source (every fitted switch
// closed; N.I.N.A. only against it, a fresh slewing reading), the hold (no
// ride-through, no freshness bound), the test box bypass kept out of it, and
// the stuck-switch watch that refuses Open.

#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "Config.h"
#include "ParkRule.h"
#include "RoofGates.h"
#include "RoofText.h"
#include "SwitchWatch.h"
#include "SwitchWatchStep.h"
#include "TestBox.h"

using namespace SQM;
using namespace SQM::Safety;
using Nina::MountReading;
using Nina::ReadingStatus;

void setUp() {}
void tearDown() {}

namespace
{
    constexpr uint64_t NOW_MS = 500000;

    std::string rejectReason(const std::string &json)
    {
        const Config base = Config::createDefault();
        std::string error;
        const auto cfg = Config::fromJson(json, &base, &error);
        return cfg ? std::string() : error;
    }

    Config applied(const std::string &json, const Config &base = Config::createDefault())
    {
        std::string error;
        const auto cfg = Config::fromJson(json, &base, &error);
        TEST_ASSERT_TRUE_MESSAGE(cfg.has_value(), error.c_str());
        return *cfg;
    }

    // Settings as stored by an older release (not strict).
    Config loadedFrom(const std::string &json)
    {
        Config loaded = Config::createDefault();
        TEST_ASSERT_TRUE(Config::applyJson(json, loaded, false));
        return loaded;
    }

    MountReading mount(bool connected, bool atPark, bool slewing, uint64_t takenAtMs = NOW_MS - 1000)
    {
        MountReading m;
        m.status = ReadingStatus::Good;
        m.connected = connected;
        m.atPark = atPark;
        m.slewing = slewing;
        m.takenAtMs = takenAtMs;
        return m;
    }

    ParkInputs switchesOnly(uint8_t fitted)
    {
        ParkInputs in;
        in.switchesOnly = true;
        in.nina = Nina::failedReading(ReadingStatus::NotConfigured, "", NOW_MS);
        in.fittedSwitches = fitted;
        in.switch1 = SwitchReading::Closed;
        in.switch2 = SwitchReading::Closed;
        in.nowMs = NOW_MS;
        return in;
    }
} // namespace

// --- settings ---

void test_default_is_nina_only(void)
{
    const Config c = Config::createDefault();
    TEST_ASSERT_TRUE(c.park.source == ParkConfig::Source::Nina);
    StaticJsonDocument<8192> doc;
    deserializeJson(doc, c.toJson());
    TEST_ASSERT_EQUAL_STRING("nina", doc["park"]["source"].as<const char *>());
}

void test_upgrade_takes_the_source_from_the_switches(void)
{
    TEST_ASSERT_TRUE(loadedFrom(R"({"park":{"switches":"none"}})").park.source == ParkConfig::Source::Nina);
    TEST_ASSERT_TRUE(loadedFrom(R"({"park":{"switches":"one"}})").park.source == ParkConfig::Source::NinaAndSwitches);
    TEST_ASSERT_TRUE(loadedFrom(R"({"park":{"switches":"two"}})").park.source == ParkConfig::Source::NinaAndSwitches);
    TEST_ASSERT_TRUE(loadedFrom(R"({"deviceName":"x"})").park.source == ParkConfig::Source::Nina);
}

void test_source_round_trips(void)
{
    const Config c = applied(R"({"park":{"source":"switches","switches":"two"}})");
    TEST_ASSERT_TRUE(c.park.source == ParkConfig::Source::Switches);
    TEST_ASSERT_TRUE(c.park.switchesOnly());
    const auto again = Config::fromJson(c.toJson());
    TEST_ASSERT_TRUE(again.has_value());
    TEST_ASSERT_TRUE(again->park.source == ParkConfig::Source::Switches);
    TEST_ASSERT_TRUE(again->park.switches == ParkConfig::Switches::Two);
}

void test_switches_alone_keep_their_old_meaning(void)
{
    // An older page or script: the switches it sends mean today's rule.
    Config base = applied(R"({"park":{"source":"switches","switches":"one"}})");
    TEST_ASSERT_TRUE(applied(R"({"park":{"switches":"two"}})", base).park.source == ParkConfig::Source::NinaAndSwitches);
    TEST_ASSERT_TRUE(applied(R"({"park":{"switches":"none"}})", base).park.source == ParkConfig::Source::Nina);
    // A save that doesn't touch the park keeps the source.
    TEST_ASSERT_TRUE(applied(R"({"park":{"timeoutSeconds":300}})", base).park.source == ParkConfig::Source::Switches);
}

void test_combinations_a_save_refuses(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "Park: N.I.N.A. only uses no park switch; choose N.I.N.A. and park switches",
        rejectReason(R"({"park":{"source":"nina","switches":"one"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Park: park switches need one or two fitted; choose N.I.N.A. only for none",
        rejectReason(R"({"park":{"source":"switches","switches":"none"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Park: park switches need one or two fitted; choose N.I.N.A. only for none",
        rejectReason(R"({"park":{"source":"ninaAndSwitches"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Park source must be nina, switches or ninaAndSwitches", rejectReason(R"({"park":{"source":"both","switches":"one"}})").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "Park source must be nina, switches or ninaAndSwitches", rejectReason(R"({"park":{"source":2,"switches":"one"}})").c_str());
    TEST_ASSERT_EQUAL_STRING("", rejectReason(R"({"park":{"source":"ninaAndSwitches","switches":"two"}})").c_str());
}

void test_test_box_park_bypass_not_with_switches_only(void)
{
    const std::string reason = rejectReason(R"({"park":{"source":"switches","switches":"one"},"testBox":{"park":true}})");
    TEST_ASSERT_EQUAL_STRING(
        "Test box: the park bypass stands in for N.I.N.A.; with park switches only there is nothing to bypass", reason.c_str());
}

void test_stored_inconsistent_settings_still_load(void)
{
    // Edited by hand: loads, and the rule below treats it safely.
    const Config c = loadedFrom(R"({"park":{"source":"switches","switches":"none"}})");
    TEST_ASSERT_TRUE(c.park.switchesOnly());
    TEST_ASSERT_TRUE(c.park.switches == ParkConfig::Switches::None);
}

// --- the park rule ---

void test_switches_only_confirms_on_the_fitted_switches(void)
{
    for (uint8_t fitted = 1; fitted <= 2; ++fitted)
    {
        const ParkResult r = evaluatePark(switchesOnly(fitted));
        TEST_ASSERT_TRUE(r.parked);
        TEST_ASSERT_TRUE(r.switchesOnly);
        TEST_ASSERT_EQUAL_STRING("parked (park switches)", describe(r).c_str());
    }
}

void test_switches_only_any_switch_open_or_unreadable(void)
{
    ParkInputs in = switchesOnly(2);
    in.switch2 = SwitchReading::Open;
    ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkSwitch2Open, r.reasons);
    TEST_ASSERT_EQUAL_STRING("not parked: Park switch 2 open", describe(r).c_str());
    in.switch2 = SwitchReading::Closed;
    in.switch1 = SwitchReading::Unreadable;
    r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkSwitch1Unreadable, r.reasons);
    // One fitted: switch 2's reading is never looked at.
    in = switchesOnly(1);
    in.switch2 = SwitchReading::Open;
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
}

void test_switches_only_none_fitted_is_never_parked(void)
{
    const ParkResult r = evaluatePark(switchesOnly(0));
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkNoSwitchFitted, r.reasons);
    TEST_ASSERT_EQUAL_STRING("not parked: no park switch fitted", describe(r).c_str());
}

void test_switches_only_ignores_nina_failures_and_not_at_park(void)
{
    ParkInputs in = switchesOnly(1);
    const ReadingStatus failures[] = {
        ReadingStatus::NotConfigured,
        ReadingStatus::Waiting,
        ReadingStatus::Unreachable,
        ReadingStatus::Unreadable,
        ReadingStatus::ApiError};
    for (ReadingStatus status : failures)
    {
        in.nina = Nina::failedReading(status, "x", NOW_MS);
        TEST_ASSERT_TRUE(evaluatePark(in).parked);
    }
    in.nina = mount(true, false, false); // not at its own park position
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
    in.nina = mount(false, false, true); // disconnected: its slewing flag means nothing
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
    in.nina = mount(true, true, true, NOW_MS - NINA_FRESH_MS - 1); // slewing, but stale
    TEST_ASSERT_TRUE(evaluatePark(in).parked);
}

void test_switches_only_fresh_slewing_vetoes(void)
{
    ParkInputs in = switchesOnly(2);
    in.nina = mount(true, true, true, NOW_MS - NINA_FRESH_MS); // at the limit: still fresh
    const ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_EQUAL_UINT32(ParkMountSlewing, r.reasons);
    TEST_ASSERT_EQUAL_STRING("not parked: Mount slewing", describe(r).c_str());
}

void test_nina_sources_unchanged(void)
{
    // N.I.N.A. and switches: the switches alone never confirm it.
    ParkInputs in = switchesOnly(2);
    in.switchesOnly = false;
    ParkResult r = evaluatePark(in);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_TRUE((r.reasons & ParkNinaNotConfigured) != 0);
    TEST_ASSERT_FALSE(r.switchesOnly);
    in.nina = mount(true, true, false);
    r = evaluatePark(in);
    TEST_ASSERT_TRUE(r.parked);
    TEST_ASSERT_EQUAL_STRING("parked", describe(r).c_str());
}

void test_switches_only_hold_has_no_ride_and_no_bound(void)
{
    ParkInputs in = switchesOnly(1);
    in.nina = Nina::failedReading(ReadingStatus::Unreachable, "timeout", NOW_MS);
    NinaMemory memory;
    rememberNina(memory, mount(true, true, false), 1);
    CloseHold hold = closeHold(in, evaluatePark(in), memory);
    TEST_ASSERT_TRUE(hold.parked);
    TEST_ASSERT_FALSE(hold.riding);
    TEST_ASSERT_EQUAL_UINT64(0, hold.freshUntilMs);
    in.switch1 = SwitchReading::Open;
    hold = closeHold(in, evaluatePark(in), memory);
    TEST_ASSERT_FALSE(hold.parked);
    TEST_ASSERT_FALSE(hold.riding); // a remembered parked reading never stands in for a switch
}

void test_switches_only_interlock_follows_the_switches(void)
{
    ParkInputs in = switchesOnly(2);
    const ParkResult r = evaluatePark(in);
    const CloseHold hold = closeHold(in, r, NinaMemory());
    RoofIo::GateInputs gates;
    gates.parked = r.parked;
    gates.ninaGrace = hold.riding;
    gates.parkFreshUntilMs = hold.freshUntilMs;
    gates.nowMs = NOW_MS + 60000; // long after any N.I.N.A. reading
    gates.fittedSwitches = 2;
    gates.switch1 = RoofIo::Contact::Closed;
    gates.switch2 = RoofIo::Contact::Closed;
    TEST_ASSERT_TRUE(RoofIo::closeMayContinue(gates));
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, RoofIo::gateCommand(Roof::Command::Close, gates));
    gates.switch2 = RoofIo::Contact::Open; // read by the control task this step
    TEST_ASSERT_FALSE(RoofIo::closeMayContinue(gates));
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::MountNotParked, RoofIo::gateCommand(Roof::Command::Close, gates));
}

void test_test_box_bypass_never_applies_with_switches_only(void)
{
    Config c = Config::createDefault();
    c.park.source = ParkConfig::Source::Switches;
    c.park.switches = ParkConfig::Switches::One;
    c.testBox.park = true;
    c.testBox.until = 2000000000u;
    const TestBox::State state = TestBox::evaluate(c.testBox, TestBox::sourcesOf(c.safety, c.park), true, 1900000000);
    TEST_ASSERT_FALSE(state.park);
    ParkInputs in = switchesOnly(1);
    in.switch1 = SwitchReading::Open;
    ParkResult r = evaluatePark(in);
    CloseHold hold = closeHold(in, r, NinaMemory());
    TestBox::applyPark(r, hold, state);
    TEST_ASSERT_FALSE(r.parked);
    TEST_ASSERT_FALSE(r.bypassed);
}

// --- the stuck-switch watch ---

void test_watch_counts_openings_and_clears_on_open(void)
{
    SwitchWatch w;
    TEST_ASSERT_TRUE(countOpening(w, 2));
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[0]);
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[1]);
    TEST_ASSERT_FALSE(watchReadings(w, 2, SwitchReading::Closed, SwitchReading::Unreadable)); // unreadable doesn't clear
    TEST_ASSERT_TRUE(watchReadings(w, 2, SwitchReading::Closed, SwitchReading::Open));
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[0]);
    TEST_ASSERT_EQUAL_UINT8(0, w.quiet[1]);
    TEST_ASSERT_FALSE(watchReadings(w, 2, SwitchReading::Closed, SwitchReading::Open)); // nothing new
}

void test_watch_only_fitted_switches(void)
{
    SwitchWatch w;
    countOpening(w, 1);
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[0]);
    TEST_ASSERT_EQUAL_UINT8(0, w.quiet[1]);
    TEST_ASSERT_FALSE(countOpening(w, 0));
    w.quiet[1] = 9; // left from when it was fitted
    TEST_ASSERT_EQUAL_UINT8(0, stuckSwitch(w, 1, true));
}

void test_watch_refuses_at_the_third_quiet_opening(void)
{
    SwitchWatch w;
    countOpening(w, 1);
    countOpening(w, 1);
    TEST_ASSERT_EQUAL_UINT8(0, stuckSwitch(w, 1, true)); // two quiet openings: allowed
    countOpening(w, 1);
    TEST_ASSERT_EQUAL_UINT8(1, stuckSwitch(w, 1, true));  // the fourth open is refused
    TEST_ASSERT_EQUAL_UINT8(0, stuckSwitch(w, 1, false)); // never with N.I.N.A. in the rule
    watchReadings(w, 1, SwitchReading::Open, SwitchReading::Unreadable);
    TEST_ASSERT_EQUAL_UINT8(0, stuckSwitch(w, 1, true));
}

void test_watch_saturates(void)
{
    SwitchWatch w;
    w.quiet[0] = 255;
    TEST_ASSERT_FALSE(countOpening(w, 1));
    TEST_ASSERT_EQUAL_UINT8(255, w.quiet[0]);
}

void test_stuck_switch_refuses_open_only(void)
{
    RoofIo::GateInputs gates;
    gates.weather = Verdict::Safe;
    gates.stuckSwitch = 2;
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::SwitchCheck, RoofIo::gateCommand(Roof::Command::Open, gates));
    TEST_ASSERT_EQUAL_STRING("parkSwitchCheck", RoofIo::gateRefusalName(RoofIo::GateRefusal::SwitchCheck));
    TEST_ASSERT_EQUAL_STRING(
        "Park switch 2 hasn't opened in the last 3 roof openings: it may be stuck or shorted. Move the mount off its park (or take "
        "the magnet away) once to check it",
        RoofIo::describeGate(RoofIo::GateRefusal::SwitchCheck, gates).c_str());
    // The weather is named first; an open under way, a jog and a close are never stopped by it.
    gates.weather = Verdict::Unsafe;
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::WeatherNotSafe, RoofIo::gateCommand(Roof::Command::Open, gates));
    gates.weather = Verdict::Safe;
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, RoofIo::openRefusal(gates));
    gates.parked = true;
    TEST_ASSERT_EQUAL(RoofIo::GateRefusal::None, RoofIo::gateCommand(Roof::Command::Close, gates));
}

// The control step's counting rule (RoofIo::stepSwitchWatch), as RoofControl and the demo run it.
namespace
{
    RoofIo::WatchStep watchStep(Roof::State before, Roof::State now)
    {
        RoofIo::WatchStep s;
        s.before = before;
        s.now = now;
        s.fitted = 1;
        s.switchesOnly = true;
        s.switch1 = RoofIo::Contact::Closed;
        return s;
    }

    // One opening and closing of the roof, the switch closed throughout.
    uint8_t openAndClose(SwitchWatch &w)
    {
        RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Closed, Roof::State::Opening));
        const uint8_t stuck = RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Opening, Roof::State::Open));
        RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Open, Roof::State::Open)); // staying open counts once
        RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Open, Roof::State::Closing));
        RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Closing, Roof::State::Closed));
        return stuck;
    }
} // namespace

void test_step_refuses_open_at_the_fourth_opening(void)
{
    SwitchWatch w;
    TEST_ASSERT_EQUAL_UINT8(0, openAndClose(w));
    TEST_ASSERT_EQUAL_UINT8(0, openAndClose(w));
    TEST_ASSERT_EQUAL_UINT8(1, openAndClose(w)); // the third opening reaches the limit
    TEST_ASSERT_EQUAL_UINT8(3, w.quiet[0]);
    // The mount leaves its park once: the switch reads open and the count clears.
    RoofIo::WatchStep moved = watchStep(Roof::State::Closed, Roof::State::Closed);
    moved.switch1 = RoofIo::Contact::Open;
    TEST_ASSERT_EQUAL_UINT8(0, RoofIo::stepSwitchWatch(w, moved));
    TEST_ASSERT_EQUAL_UINT8(0, w.quiet[0]);
}

void test_step_counts_only_a_real_opening(void)
{
    SwitchWatch w;
    RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Unknown, Roof::State::Open)); // boot at the open magnet
    TEST_ASSERT_EQUAL_UINT8(0, w.quiet[0]);
    RoofIo::stepSwitchWatch(w, watchStep(Roof::State::Stopped, Roof::State::Open)); // from part-way
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[0]);
    RoofIo::WatchStep calibrating = watchStep(Roof::State::Opening, Roof::State::Open);
    calibrating.calibrating = true; // a calibration leg
    RoofIo::stepSwitchWatch(w, calibrating);
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[0]);
    RoofIo::WatchStep withNina = watchStep(Roof::State::Opening, Roof::State::Open);
    withNina.switchesOnly = false; // N.I.N.A. in the rule: nothing counted, nothing refused
    TEST_ASSERT_EQUAL_UINT8(0, RoofIo::stepSwitchWatch(w, withNina));
    TEST_ASSERT_EQUAL_UINT8(1, w.quiet[0]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_is_nina_only);
    RUN_TEST(test_upgrade_takes_the_source_from_the_switches);
    RUN_TEST(test_source_round_trips);
    RUN_TEST(test_switches_alone_keep_their_old_meaning);
    RUN_TEST(test_combinations_a_save_refuses);
    RUN_TEST(test_test_box_park_bypass_not_with_switches_only);
    RUN_TEST(test_stored_inconsistent_settings_still_load);
    RUN_TEST(test_switches_only_confirms_on_the_fitted_switches);
    RUN_TEST(test_switches_only_any_switch_open_or_unreadable);
    RUN_TEST(test_switches_only_none_fitted_is_never_parked);
    RUN_TEST(test_switches_only_ignores_nina_failures_and_not_at_park);
    RUN_TEST(test_switches_only_fresh_slewing_vetoes);
    RUN_TEST(test_nina_sources_unchanged);
    RUN_TEST(test_switches_only_hold_has_no_ride_and_no_bound);
    RUN_TEST(test_switches_only_interlock_follows_the_switches);
    RUN_TEST(test_test_box_bypass_never_applies_with_switches_only);
    RUN_TEST(test_watch_counts_openings_and_clears_on_open);
    RUN_TEST(test_watch_only_fitted_switches);
    RUN_TEST(test_watch_refuses_at_the_third_quiet_opening);
    RUN_TEST(test_watch_saturates);
    RUN_TEST(test_stuck_switch_refuses_open_only);
    RUN_TEST(test_step_refuses_open_at_the_fourth_opening);
    RUN_TEST(test_step_counts_only_a_real_opening);
    return UNITY_END();
}
