#include <unity.h>

#include <ArduinoJson.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>

#include "Config.h" // for the library finder: RoofLogic builds with ConfigModel
#include "RoofAlerts.h"
#include "RoofText.h"

// The close sequence and the roof as alert events (spec 007): spec 005's
// critical decisions as typed alerts that keep their wording and reason
// (FR-002, FR-005), and the roof's conditions for the alert engine with the
// value and the limit, or the input that failed (FR-007 to FR-011,
// constitution III).

using namespace SQM;
using namespace SQM::CloseSeq;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    std::string var(const Alerts::Alert &alert, const char *name)
    {
        for (const auto &v : alert.vars)
            if (v.first == name)
                return v.second;
        return "<missing>";
    }

    RoofIo::Snapshot healthy()
    {
        RoofIo::Snapshot snap;
        snap.status.state = Roof::State::Open;
        snap.supplyVolts = 12.4f;
        snap.minSupplyVolts = 11.0f;
        return snap;
    }

    Safety::WeatherVerdict safeWeather()
    {
        Safety::WeatherVerdict weather;
        weather.verdict = Safety::Verdict::Safe;
        weather.http.kind = Safety::SourceKind::SqmeterHttp;
        weather.http.state = Safety::SourceState::Good;
        weather.http.sqmeterSafe = true;
        weather.mqtt.kind = Safety::SourceKind::SqmeterMqtt;
        weather.mqtt.state = Safety::SourceState::Off;
        return weather;
    }
} // namespace

// --- Critical decisions ---

void test_every_critical_event_has_a_type(void)
{
    Alerts::AlertType type = Alerts::AlertType::Test;
    TEST_ASSERT_TRUE(alertTypeFor(Event::ParkNotConfirmed, type));
    TEST_ASSERT_EQUAL(Alerts::AlertType::MountNotParked, type);
    for (Event event : {Event::CloseFailed, Event::CannotClose, Event::StoppedByPerson})
    {
        type = Alerts::AlertType::Test;
        TEST_ASSERT_TRUE(alertTypeFor(event, type));
        TEST_ASSERT_EQUAL(Alerts::AlertType::RoofNotClosed, type);
    }
    for (Event event : {Event::None, Event::Started, Event::EndedSafe, Event::Closed})
        TEST_ASSERT_FALSE(alertTypeFor(event, type));
    // Every event the sequence calls critical has a critical alert type.
    for (int e = 0; e < 8; ++e)
    {
        const Event event = static_cast<Event>(e);
        TEST_ASSERT_EQUAL(isCritical(event), alertTypeFor(event, type));
        if (isCritical(event))
            TEST_ASSERT_TRUE(Alerts::isCritical(type));
    }
}

void test_critical_alert_keeps_spec_005_wording_and_variables(void)
{
    Decision park;
    park.event = Event::ParkNotConfirmed;
    park.cause = Cause::ParkTimeout;
    park.limitMs = 240000;
    const std::string why = "not parked: N.I.N.A. unreachable: connection refused";
    const Alerts::Alert alert = criticalAlert(park, why);
    const AlertText text = alertFor(park, why);
    TEST_ASSERT_EQUAL(Alerts::AlertType::MountNotParked, alert.type);
    TEST_ASSERT_EQUAL(Alerts::AlertLevel::Wake, alert.level);
    TEST_ASSERT_EQUAL_STRING(text.title.c_str(), alert.title.c_str());
    TEST_ASSERT_EQUAL_STRING(text.message.c_str(), alert.message.c_str());
    TEST_ASSERT_EQUAL_STRING(why.c_str(), var(alert, "reason").c_str());
    TEST_ASSERT_EQUAL_STRING("240 s", var(alert, "limit").c_str());
    TEST_ASSERT_EQUAL_STRING(text.message.c_str(), var(alert, "details").c_str());
    TEST_ASSERT_EQUAL_STRING("no", var(alert, "reminder").c_str());
}

// The shared variable list (lib/AlertLogic/template-variables.json) for the
// critical events is what criticalAlert() fills.
void test_critical_alert_fills_the_shared_variables(void)
{
    std::ifstream in("lib/AlertLogic/template-variables.json");
    std::stringstream text;
    text << in.rdbuf();
    DynamicJsonDocument doc(4096);
    TEST_ASSERT_FALSE(deserializeJson(doc, text.str()));
    Decision park;
    park.event = Event::ParkNotConfirmed;
    Decision close;
    close.event = Event::CloseFailed;
    for (const Decision &decision : {park, close})
    {
        const Alerts::Alert alert = criticalAlert(decision, "why");
        const char *event = Alerts::alertTypeName(alert.type);
        JsonArrayConst names = doc["events"][event].as<JsonArrayConst>();
        TEST_ASSERT_TRUE_MESSAGE(names.size() > 0, event);
        for (JsonVariantConst name : names)
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, var(alert, name.as<const char *>()).compare("<missing>"), name.as<const char *>());
    }
}

void test_critical_alert_reason_falls_back_to_the_cause(void)
{
    Decision held;
    held.event = Event::StoppedByPerson;
    TEST_ASSERT_EQUAL_STRING("Stopped by a person", var(criticalAlert(held, ""), "reason").c_str());
    Decision unknown;
    unknown.event = Event::CannotClose;
    unknown.cause = Cause::PositionUnknown;
    TEST_ASSERT_EQUAL_STRING("Position unknown", var(criticalAlert(unknown, ""), "reason").c_str());
    unknown.cause = Cause::RoofNotStarted;
    TEST_ASSERT_EQUAL_STRING("Roof controller not started", var(criticalAlert(unknown, ""), "reason").c_str());
    Decision close;
    close.event = Event::CloseFailed;
    close.cause = Cause::Refused;
    TEST_ASSERT_EQUAL_STRING("Supply 10.9 V < 11.0 V", var(criticalAlert(close, "Supply 10.9 V < 11.0 V"), "reason").c_str());
    TEST_ASSERT_EQUAL_STRING("", var(criticalAlert(close, "Supply 10.9 V < 11.0 V"), "limit").c_str());
}

void test_critical_reminder_is_marked(void)
{
    Decision held;
    held.event = Event::StoppedByPerson;
    held.reminder = true;
    const Alerts::Alert alert = criticalAlert(held, "");
    TEST_ASSERT_EQUAL_STRING("yes", var(alert, "reminder").c_str());
    TEST_ASSERT_EQUAL_STRING("Still: Automatic close stopped", alert.title.c_str());
    TEST_ASSERT_EQUAL(Alerts::AlertType::RoofNotClosed, alert.type);
}

void test_attempting_phases(void)
{
    TEST_ASSERT_TRUE(attempting(Phase::WaitPark));
    TEST_ASSERT_TRUE(attempting(Phase::Closing));
    TEST_ASSERT_TRUE(attempting(Phase::RetryWait));
    TEST_ASSERT_FALSE(attempting(Phase::Idle));
    TEST_ASSERT_FALSE(attempting(Phase::HeldByPerson));
    TEST_ASSERT_FALSE(attempting(Phase::CannotMove));
    TEST_ASSERT_FALSE(attempting(Phase::Halted));
}

// --- Conditions ---

void test_conditions_all_clear(void)
{
    Alerts::RoofInputs roof;
    updateConditions(roof, healthy(), safeWeather(), SequenceView{});
    TEST_ASSERT_TRUE(roof.known);
    TEST_ASSERT_FALSE(roof.faultLatched);
    TEST_ASSERT_FALSE(roof.supplyLow);
    TEST_ASSERT_FALSE(roof.sourceLost);
    TEST_ASSERT_FALSE(roof.closing);
    TEST_ASSERT_EQUAL(0, roof.closedCount);
    TEST_ASSERT_TRUE(roof.fault.empty() && roof.supply.empty() && roof.source.empty());
}

void test_conditions_fault_with_value_and_limit(void)
{
    RoofIo::Snapshot snap = healthy();
    snap.status.state = Roof::State::Error;
    snap.fault.fault = {Roof::FaultKind::Timeout, 61000.0f, 60000.0f};
    snap.status.fault = snap.fault.fault;
    Alerts::RoofInputs roof;
    updateConditions(roof, snap, safeWeather(), SequenceView{});
    TEST_ASSERT_TRUE(roof.faultLatched);
    TEST_ASSERT_EQUAL_STRING(RoofIo::describeFault(snap.fault).c_str(), roof.fault.c_str());
    TEST_ASSERT_NOT_NULL(strstr(roof.fault.c_str(), "61"));
    TEST_ASSERT_NOT_NULL(strstr(roof.fault.c_str(), "60"));
}

void test_conditions_supply_boundary_and_unreadable(void)
{
    RoofIo::Snapshot snap = healthy();
    Alerts::RoofInputs roof;
    snap.supplyVolts = 11.0f; // at the minimum: not low
    updateConditions(roof, snap, safeWeather(), SequenceView{});
    TEST_ASSERT_FALSE(roof.supplyLow);
    snap.supplyVolts = 10.9f;
    updateConditions(roof, snap, safeWeather(), SequenceView{});
    TEST_ASSERT_TRUE(roof.supplyLow);
    TEST_ASSERT_EQUAL_STRING("Supply 10.9 V < 11.0 V", roof.supply.c_str());
    snap.supplyVolts = NAN;
    updateConditions(roof, snap, safeWeather(), SequenceView{});
    TEST_ASSERT_TRUE(roof.supplyLow);
    TEST_ASSERT_EQUAL_STRING("Supply unreadable (minimum 11.0 V)", roof.supply.c_str());
}

void test_conditions_lost_sources(void)
{
    Safety::WeatherVerdict weather = safeWeather();
    const Safety::SourceState lostStates[] = {
        Safety::SourceState::Stale, Safety::SourceState::Unreachable, Safety::SourceState::Unreadable, Safety::SourceState::Offline};
    for (Safety::SourceState state : lostStates)
    {
        weather.http.state = state;
        weather.http.detail = "connection refused";
        weather.http.ageMs = 75000;
        weather.http.limitMs = 30000;
        Alerts::RoofInputs roof;
        updateConditions(roof, healthy(), weather, SequenceView{});
        TEST_ASSERT_TRUE(roof.sourceLost);
        TEST_ASSERT_EQUAL_STRING(Safety::describeSource(weather.http).c_str(), roof.source.c_str());
    }
    // Waiting for a first reply, good and off are not lost.
    for (Safety::SourceState state : {Safety::SourceState::Waiting, Safety::SourceState::Good, Safety::SourceState::Off})
    {
        weather.http.state = state;
        TEST_ASSERT_EQUAL_STRING("", lostSources(weather).c_str());
    }
    // Two sources and the rain contact, all named.
    weather.http.state = Safety::SourceState::Unreachable;
    weather.mqtt.state = Safety::SourceState::Offline;
    weather.rainContact = Safety::RainContact::Unreadable;
    const std::string text = lostSources(weather);
    TEST_ASSERT_NOT_NULL(strstr(text.c_str(), Safety::describeSource(weather.http).c_str()));
    TEST_ASSERT_NOT_NULL(strstr(text.c_str(), Safety::describeSource(weather.mqtt).c_str()));
    TEST_ASSERT_NOT_NULL(strstr(text.c_str(), "Rain contact unreadable"));
}

void test_conditions_no_source_on_is_not_lost(void)
{
    Safety::WeatherVerdict weather;
    weather.verdict = Safety::Verdict::Unsafe;
    weather.reasons = Safety::ReasonNoWeatherSource;
    weather.http.state = Safety::SourceState::Off;
    weather.mqtt.state = Safety::SourceState::Off;
    Alerts::RoofInputs roof;
    updateConditions(roof, healthy(), weather, SequenceView{});
    TEST_ASSERT_FALSE(roof.sourceLost);
    weather.rainContact = Safety::RainContact::Raining;
    updateConditions(roof, healthy(), weather, SequenceView{});
    TEST_ASSERT_FALSE(roof.sourceLost);
}

void test_conditions_closing_and_the_weather_kept_for_the_close(void)
{
    Safety::WeatherVerdict rain = safeWeather();
    rain.verdict = Safety::Verdict::Unsafe;
    rain.reasons = Safety::ReasonRain;
    Alerts::RoofInputs roof;
    updateConditions(roof, healthy(), rain, SequenceView{Phase::WaitPark, 0});
    TEST_ASSERT_TRUE(roof.closing);
    const std::string started = roof.weather;
    TEST_ASSERT_EQUAL_STRING(Safety::describe(rain).c_str(), started.c_str());
    // Closed: idle, the count up, the weather text kept although it is safe now.
    updateConditions(roof, healthy(), safeWeather(), SequenceView{Phase::Idle, 1});
    TEST_ASSERT_FALSE(roof.closing);
    TEST_ASSERT_EQUAL(1, roof.closedCount);
    TEST_ASSERT_EQUAL_STRING(started.c_str(), roof.weather.c_str());
    // Stood down or unable to move: not "closing".
    for (Phase phase : {Phase::HeldByPerson, Phase::CannotMove, Phase::Halted})
    {
        updateConditions(roof, healthy(), rain, SequenceView{phase, 1});
        TEST_ASSERT_FALSE(roof.closing);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_critical_event_has_a_type);
    RUN_TEST(test_critical_alert_keeps_spec_005_wording_and_variables);
    RUN_TEST(test_critical_alert_fills_the_shared_variables);
    RUN_TEST(test_critical_alert_reason_falls_back_to_the_cause);
    RUN_TEST(test_critical_reminder_is_marked);
    RUN_TEST(test_attempting_phases);
    RUN_TEST(test_conditions_all_clear);
    RUN_TEST(test_conditions_fault_with_value_and_limit);
    RUN_TEST(test_conditions_supply_boundary_and_unreadable);
    RUN_TEST(test_conditions_lost_sources);
    RUN_TEST(test_conditions_no_source_on_is_not_lost);
    RUN_TEST(test_conditions_closing_and_the_weather_kept_for_the_close);
    return UNITY_END();
}
