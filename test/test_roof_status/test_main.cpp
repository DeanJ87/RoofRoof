// The roof's wording (constitution III: every fault and refusal states the
// measured value and the limit, or the input that failed) and its status
// section (spec 003 FR-013, FR-022).

#include <ArduinoJson.h>
#include <cmath>
#include <string>
#include <unity.h>

#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "RoofStatus.h"
#include "RoofText.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    FaultRecord logic(Roof::FaultKind kind, float measured, float limit)
    {
        FaultRecord r;
        r.fault.kind = kind;
        r.fault.measured = measured;
        r.fault.limit = limit;
        return r;
    }

    void assertText(const char *expected, const std::string &actual)
    {
        TEST_ASSERT_EQUAL_STRING(expected, actual.c_str());
    }

    Roof::CommandResult refused(Roof::Refusal reason, float measured, float limit)
    {
        Roof::CommandResult r;
        r.accepted = false;
        r.reason = reason;
        r.measured = measured;
        r.limit = limit;
        return r;
    }
} // namespace

void test_every_fault_text_has_value_and_limit(void)
{
    using K = Roof::FaultKind;
    assertText("", describeFault(FaultRecord()));
    assertText("No target magnet after 65.0 s >= 65.0 s timeout", describeFault(logic(K::Timeout, 65000, 65000)));
    assertText("Still on the start magnet after 10.0 s >= 10.0 s", describeFault(logic(K::DidNotLeave, 10000, 10000)));
    assertText("Wrong magnet: read the open magnet moving to closed", describeFault(logic(K::WrongMagnet, 1, 0)));
    assertText("Wrong magnet: read the closed magnet moving to open", describeFault(logic(K::WrongMagnet, 0, 1)));
    assertText("Hall sensor at a rail: 4080 counts >= 4065", describeFault(logic(K::HallRail, 4080, 4065)));
    assertText("Hall sensor at a rail: 12 counts <= 30", describeFault(logic(K::HallRail, 12, 30)));
    assertText("Motor current 12.3 A >= 10.0 A", describeFault(logic(K::OverCurrent, 12.3f, 10)));
    assertText("Motor current unreadable (limit 10.0 A)", describeFault(logic(K::CurrentUnreadable, NAN, 10)));
    assertText("Supply 10.9 V < 11.0 V", describeFault(logic(K::SupplyLow, 10.9f, 11)));
    assertText("Supply unreadable (minimum 11.0 V)", describeFault(logic(K::SupplyLow, NAN, 11)));
    assertText("Control step 140 ms late > 100 ms", describeFault(logic(K::LateStep, 140, 100)));
    assertText("Driven 95.0 s without reaching a magnet >= 95.0 s budget", describeFault(logic(K::DriveBudget, 95000, 95000)));
}

void test_external_fault_texts_by_cause(void)
{
    assertText("Control step 140 ms late > 100 ms (watchdog)", describeFault(externalRecord(FaultCause::Watchdog, 140, 100)));
    assertText(
        "Park lost during the close: park switch 1 not closed",
        describeFault(externalRecord(FaultCause::ParkInterlock, InterlockSwitch1, 0)));
    assertText(
        "Park lost during the close: park not confirmed, park switch 2 not closed",
        describeFault(externalRecord(FaultCause::ParkInterlock, InterlockParkResult | InterlockSwitch2, 0)));
    assertText(
        "Stored fault unreadable: the fault saved before the restart couldn't be read back", describeFault(storedUnreadableRecord()));
    assertText(
        "Watchdog timer stopped: last check 120 ms ago > 100 ms", describeFault(externalRecord(FaultCause::WatchdogStopped, 120, 100)));
    assertText("Fault raised: 3 (limit 2)", describeFault(externalRecord(FaultCause::Logic, 3, 2)));
    // Spec 011: the measured supply and the limit, or the unreadable input.
    assertText(
        "Bench firmware on a powered roof: supply 12.4 V >= 6.0 V", describeFault(externalRecord(FaultCause::BenchSupply, 12.4f, 6.0f)));
    assertText(
        "Bench firmware on a powered roof? Supply input unreadable (limit 6.0 V)",
        describeFault(externalRecord(FaultCause::BenchSupply, NAN, 6.0f)));
}

void test_refusal_texts(void)
{
    using R = Roof::Refusal;
    assertText("", describeRefusal(Roof::CommandResult()));
    assertText("A fault is latched: a person must clear it first", describeRefusal(refused(R::FaultLatched, 0, 0)));
    assertText("Hall reading not settled yet: try again in a moment", describeRefusal(refused(R::HallNotSettled, 0, 0)));
    assertText("Position unknown: only a person can move the roof", describeRefusal(refused(R::PositionUnknown, 0, 0)));
    assertText("Supply 10.9 V < 11.0 V", describeRefusal(refused(R::SupplyLow, 10.9f, 11)));
    assertText("Supply unreadable (minimum 11.0 V)", describeRefusal(refused(R::SupplyUnreadable, 0, 11)));
    assertText("Motor current unreadable (limit 10.0 A)", describeRefusal(refused(R::CurrentUnreadable, 0, 10)));
    assertText("Roof settings invalid: fix them on the settings page", describeRefusal(refused(R::SettingsInvalid, 0, 0)));
    assertText("Only a person can clear a fault", describeRefusal(refused(R::NotAPerson, 0, 0)));
    assertText("The roof is moving", describeRefusal(refused(R::Moving, 0, 0)));
}

void test_gate_texts_name_the_failed_input(void)
{
    GateInputs in;
    in.weatherText = "unsafe: rain";
    assertText("Weather not safe: unsafe: rain", describeGate(GateRefusal::WeatherNotSafe, in));
    in.weatherText.clear();
    assertText("Weather not safe: no verdict yet", describeGate(GateRefusal::WeatherNotSafe, in));
    in.parked = false;
    in.parkText = "not parked: N.I.N.A. not configured";
    assertText("Mount not parked: park not confirmed (not parked: N.I.N.A. not configured)", describeGate(GateRefusal::MountNotParked, in));
    in.parked = true;
    in.fittedSwitches = 2;
    in.switch1 = Contact::Closed;
    in.switch2 = Contact::Open;
    assertText("Mount not parked: park switch 2 not closed", describeGate(GateRefusal::MountNotParked, in));
    assertText("", describeGate(GateRefusal::None, in));
}

// --- Status section ---------------------------------------------------------

namespace
{
    Snapshot closedRoof()
    {
        Snapshot s;
        s.status.state = Roof::State::Closed;
        s.status.positionKnown = true;
        s.status.positionMm = 0.0f;
        s.status.hall = Roof::HallLevel::Low;
        s.status.hallCounts = 700;
        s.supplyVolts = 12.437f;
        s.fittedSwitches = 1;
        s.switch1 = Contact::Closed;
        s.rainFitted = true;
        s.rain = Contact::Open;
        return s;
    }

    // The section as a parsed document.
    DynamicJsonDocument write(const Snapshot &s)
    {
        DynamicJsonDocument doc(2048);
        writeStatus(doc.createNestedObject("roof"), s);
        std::string text;
        serializeJson(doc, text);
        DynamicJsonDocument parsed(2048);
        TEST_ASSERT_FALSE(deserializeJson(parsed, text));
        TEST_ASSERT_TRUE_MESSAGE(text.size() < 1024, "the roof section stays well under 1 KB");
        return parsed;
    }
} // namespace

void test_status_closed_roof(void)
{
    DynamicJsonDocument doc = write(closedRoof());
    JsonObject roof = doc["roof"];
    TEST_ASSERT_EQUAL_STRING("closed", roof["state"]);
    TEST_ASSERT_TRUE(roof["positionKnown"].as<bool>());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, roof["positionMm"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, roof["positionPercent"].as<float>());
    TEST_ASSERT_TRUE(roof["target"].isNull());
    TEST_ASSERT_EQUAL_STRING("low", roof["hall"]["level"]);
    TEST_ASSERT_EQUAL(700, roof["hall"]["counts"].as<int>());
    TEST_ASSERT_EQUAL_FLOAT(12.44f, roof["supplyVolts"].as<float>());
    TEST_ASSERT_FALSE(roof["supplyLow"].as<bool>());
    TEST_ASSERT_FALSE(roof["currentSense"].as<bool>());
    TEST_ASSERT_TRUE(roof["motorAmps"].isNull()); // current sense off
    TEST_ASSERT_EQUAL(1, roof["parkSwitches"]["fitted"].as<int>());
    TEST_ASSERT_EQUAL_STRING("closed", roof["parkSwitches"]["switch1"]);
    TEST_ASSERT_EQUAL_STRING("notFitted", roof["parkSwitches"]["switch2"]);
    TEST_ASSERT_EQUAL_STRING("dry", roof["rainContact"]);
    TEST_ASSERT_TRUE(roof["fault"].isNull());
    TEST_ASSERT_FALSE(roof["closePending"].as<bool>());
    TEST_ASSERT_TRUE(roof.containsKey("closePending"));
    TEST_ASSERT_EQUAL(0, roof["watchdogTrips"].as<int>());
    TEST_ASSERT_TRUE(roof.containsKey("maxStepGapMs"));
    TEST_ASSERT_TRUE(roof.containsKey("maxWatchdogBeatAgeMs"));
    TEST_ASSERT_EQUAL_STRING("off", roof["simulation"]); // spec 011: the real roof
}

// Spec 011 FR-013, FR-026: a simulated roof says which.
void test_status_simulation(void)
{
    RoofIo::Snapshot snap = closedRoof();
    snap.simulation = "bench";
    TEST_ASSERT_EQUAL_STRING("bench", write(snap)["roof"]["simulation"]);
    snap.simulation = "demo";
    TEST_ASSERT_EQUAL_STRING("demo", write(snap)["roof"]["simulation"]);
    snap.simulation = nullptr;
    TEST_ASSERT_EQUAL_STRING("off", write(snap)["roof"]["simulation"]);
}

void test_status_opening_part_way(void)
{
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Opening;
    s.status.positionMm = 462.5f;
    s.status.target = Roof::End::Open;
    s.status.driving = true;
    s.status.moveElapsedMs = 12000;
    s.status.moveTimeoutMs = 66000;
    s.currentSense = true;
    s.motorAmps = 4.256f;
    DynamicJsonDocument doc = write(s);
    JsonObject roof = doc["roof"];
    TEST_ASSERT_EQUAL_STRING("opening", roof["state"]);
    TEST_ASSERT_EQUAL_STRING("open", roof["target"]);
    TEST_ASSERT_EQUAL_FLOAT(463.0f, roof["positionMm"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(25.0f, roof["positionPercent"].as<float>());
    TEST_ASSERT_TRUE(roof["driving"].as<bool>());
    TEST_ASSERT_EQUAL(12000, roof["moveElapsedMs"].as<int>());
    TEST_ASSERT_EQUAL(66000, roof["moveTimeoutMs"].as<int>());
    TEST_ASSERT_EQUAL_FLOAT(4.26f, roof["motorAmps"].as<float>());
}

void test_status_unknown_and_unreadable_are_null(void)
{
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Unknown;
    s.status.positionKnown = false;
    s.supplyVolts = NAN;
    s.currentSense = true;
    s.motorAmps = NAN;
    s.switch1 = Contact::Unreadable;
    s.rain = Contact::Unreadable;
    DynamicJsonDocument doc = write(s);
    JsonObject roof = doc["roof"];
    TEST_ASSERT_TRUE(roof["positionMm"].isNull());
    TEST_ASSERT_TRUE(roof["positionPercent"].isNull());
    TEST_ASSERT_TRUE(roof["supplyVolts"].isNull());
    TEST_ASSERT_TRUE(roof["supplyLow"].as<bool>());
    TEST_ASSERT_TRUE(roof["motorAmps"].isNull());
    TEST_ASSERT_EQUAL_STRING("unreadable", roof["parkSwitches"]["switch1"]);
    TEST_ASSERT_EQUAL_STRING("unreadable", roof["rainContact"]);
}

void test_status_supply_low_boundary(void)
{
    Snapshot s = closedRoof();
    s.minSupplyVolts = 11.0f;
    s.supplyVolts = 11.0f;
    TEST_ASSERT_FALSE(supplyLow(s)); // at the minimum is fine (spec 002 faults below it)
    s.supplyVolts = 10.99f;
    TEST_ASSERT_TRUE(supplyLow(s));
    s.supplyVolts = 17.0f; // implausible: unreadable
    TEST_ASSERT_TRUE(supplyLow(s));
    DynamicJsonDocument doc = write(s);
    TEST_ASSERT_TRUE(doc["roof"]["supplyVolts"].isNull());
}

void test_status_fault_with_cause_and_text(void)
{
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Error;
    s.status.positionKnown = false;
    s.status.fault.kind = Roof::FaultKind::External;
    s.status.fault.measured = 140.0f;
    s.status.fault.limit = 100.0f;
    s.fault = externalRecord(FaultCause::Watchdog, 140.0f, 100.0f);
    s.watchdogTrips = 1;
    DynamicJsonDocument doc = write(s);
    JsonObject fault = doc["roof"]["fault"];
    TEST_ASSERT_EQUAL_STRING("external", fault["kind"]);
    TEST_ASSERT_EQUAL_STRING("watchdog", fault["cause"]);
    TEST_ASSERT_EQUAL_FLOAT(140.0f, fault["measured"].as<float>());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, fault["limit"].as<float>());
    TEST_ASSERT_EQUAL_STRING("Control step 140 ms late > 100 ms (watchdog)", fault["text"]);
    TEST_ASSERT_EQUAL(1, doc["roof"]["watchdogTrips"].as<int>());
}

void test_status_fault_measured_nan_is_null(void)
{
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Error;
    s.status.fault.kind = Roof::FaultKind::SupplyLow;
    s.status.fault.measured = NAN;
    s.status.fault.limit = 11.0f;
    DynamicJsonDocument doc = write(s);
    TEST_ASSERT_TRUE(doc["roof"]["fault"]["measured"].isNull());
    TEST_ASSERT_EQUAL_STRING("logic", doc["roof"]["fault"]["cause"]);
    TEST_ASSERT_EQUAL_STRING("Supply unreadable (minimum 11.0 V)", doc["roof"]["fault"]["text"]);
}

void test_status_rain_not_fitted(void)
{
    Snapshot s = closedRoof();
    s.rainFitted = false;
    s.fittedSwitches = 0;
    DynamicJsonDocument doc = write(s);
    TEST_ASSERT_EQUAL_STRING("notFitted", doc["roof"]["rainContact"]);
    TEST_ASSERT_EQUAL_STRING("notFitted", doc["roof"]["parkSwitches"]["switch1"]);
    s.rainFitted = true;
    s.rain = Contact::Closed;
    TEST_ASSERT_EQUAL_STRING("raining", write(s)["roof"]["rainContact"]);
}

// Spec 008 FR-011: the checks the commands use, and the automatic close's phase.
void test_status_checks_and_phase(void)
{
    Snapshot s = closedRoof();
    DynamicJsonDocument fresh = write(s);
    TEST_ASSERT_EQUAL_STRING("idle", fresh["roof"]["closePhase"]);
    TEST_ASSERT_EQUAL_STRING("unknown", fresh["roof"]["weather"]);
    TEST_ASSERT_FALSE(fresh["roof"]["parkConfirmed"].as<bool>());
    s.closePhase = "waitPark";
    s.weather = Safety::Verdict::Unsafe;
    s.weatherText = "unsafe: rain";
    s.parkConfirmed = false;
    s.parkText = "Mount not parked: park not confirmed";
    DynamicJsonDocument doc = write(s);
    JsonObject roof = doc["roof"];
    TEST_ASSERT_EQUAL_STRING("waitPark", roof["closePhase"]);
    TEST_ASSERT_EQUAL_STRING("unsafe", roof["weather"]);
    TEST_ASSERT_EQUAL_STRING("unsafe: rain", roof["weatherText"]);
    TEST_ASSERT_FALSE(roof["parkConfirmed"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("Mount not parked: park not confirmed", roof["parkText"]);
    s.closePhase = nullptr; // never left unset by the firmware; still a name
    s.weather = Safety::Verdict::Safe;
    s.parkConfirmed = true;
    DynamicJsonDocument other = write(s);
    TEST_ASSERT_EQUAL_STRING("idle", other["roof"]["closePhase"]);
    TEST_ASSERT_EQUAL_STRING("safe", other["roof"]["weather"]);
    TEST_ASSERT_TRUE(other["roof"]["parkConfirmed"].as<bool>());
}

// The park is confirmed only when the close gate would pass now (constitution II).
void test_apply_checks_follows_the_gates(void)
{
    GateInputs g;
    Snapshot s;
    applyChecks(s, g);
    TEST_ASSERT_TRUE(s.weather == Safety::Verdict::Unknown);
    TEST_ASSERT_EQUAL_STRING("no verdict yet", s.weatherText.c_str());
    TEST_ASSERT_FALSE(s.parkConfirmed);
    TEST_ASSERT_EQUAL_STRING("Mount not parked: park not confirmed", s.parkText.c_str());

    g.weather = Safety::Verdict::Safe;
    g.weatherText = "safe";
    g.parked = true;
    g.parkText = "parked (N.I.N.A. AtPark)";
    applyChecks(s, g);
    TEST_ASSERT_TRUE(s.weather == Safety::Verdict::Safe);
    TEST_ASSERT_EQUAL_STRING("safe", s.weatherText.c_str());
    TEST_ASSERT_TRUE(s.parkConfirmed);
    TEST_ASSERT_EQUAL_STRING("parked (N.I.N.A. AtPark)", s.parkText.c_str());

    // N.I.N.A. says parked but a fitted switch is open: not confirmed, and why.
    g.fittedSwitches = 1;
    g.switch1 = Contact::Open;
    applyChecks(s, g);
    TEST_ASSERT_FALSE(s.parkConfirmed);
    TEST_ASSERT_EQUAL_STRING(describeGate(GateRefusal::MountNotParked, g).c_str(), s.parkText.c_str());
    TEST_ASSERT_TRUE(s.parkText.find("switch 1") != std::string::npos);
}

// Long texts (N.I.N.A.'s or SQMeter's own error details) are cut, never inside a UTF-8 character.
void test_apply_checks_caps_long_texts(void)
{
    GateInputs g;
    g.weather = Safety::Verdict::Unsafe;
    g.weatherText = std::string(500, 'w');
    g.parked = false;
    g.parkText = std::string(196, 'p') + "\xC3\xA9\xC3\xA9" + std::string(300, 'p'); // "é" across the cut
    Snapshot s;
    applyChecks(s, g);
    TEST_ASSERT_EQUAL_size_t(MAX_CHECK_TEXT, s.weatherText.size());
    TEST_ASSERT_EQUAL_STRING("...", s.weatherText.substr(s.weatherText.size() - 3).c_str());
    TEST_ASSERT_TRUE(s.parkText.size() <= MAX_CHECK_TEXT);
    const std::string kept = s.parkText.substr(0, s.parkText.size() - 3);
    TEST_ASSERT_TRUE((static_cast<unsigned char>(kept.back()) & 0xC0) != 0xC0); // no lead byte left without its tail
    g.weatherText = std::string(MAX_CHECK_TEXT, 'x');
    applyChecks(s, g);
    TEST_ASSERT_EQUAL_STRING(g.weatherText.c_str(), s.weatherText.c_str()); // at the limit: unchanged
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_fault_text_has_value_and_limit);
    RUN_TEST(test_external_fault_texts_by_cause);
    RUN_TEST(test_refusal_texts);
    RUN_TEST(test_gate_texts_name_the_failed_input);
    RUN_TEST(test_status_closed_roof);
    RUN_TEST(test_status_simulation);
    RUN_TEST(test_status_opening_part_way);
    RUN_TEST(test_status_unknown_and_unreadable_are_null);
    RUN_TEST(test_status_supply_low_boundary);
    RUN_TEST(test_status_fault_with_cause_and_text);
    RUN_TEST(test_status_fault_measured_nan_is_null);
    RUN_TEST(test_status_rain_not_fitted);
    RUN_TEST(test_status_checks_and_phase);
    RUN_TEST(test_apply_checks_follows_the_gates);
    RUN_TEST(test_apply_checks_caps_long_texts);
    return UNITY_END();
}
