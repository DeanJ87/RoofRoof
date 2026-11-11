// The close-on-unsafe sequence (spec 005): the alert wording, the names and
// the mapping of the safety and roof inputs (constitution III: every alert
// states the value and the limit, or the input that failed).

#include "../close_rig/CloseRig.h"

using namespace CloseTest;

void setUp(void) {}
void tearDown(void) {}

// --- Wording and mapping ---

void test_alert_text_names_the_step_and_the_reason()
{
    Decision park;
    park.event = Event::ParkNotConfirmed;
    park.cause = Cause::ParkTimeout;
    park.limitMs = 240000;
    AlertText text = alertFor(park, "not parked: N.I.N.A. unreachable: connection refused");
    TEST_ASSERT_EQUAL_STRING("Mount didn't park", text.title.c_str());
    TEST_ASSERT_NOT_NULL(strstr(text.message.c_str(), "within 240 s"));
    TEST_ASSERT_NOT_NULL(strstr(text.message.c_str(), "N.I.N.A. unreachable"));
    TEST_ASSERT_NOT_NULL(strstr(text.message.c_str(), "stays open"));

    park.cause = Cause::NinaNotConfigured;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(park, "").message.c_str(), "park.nina.url"));

    Decision close;
    close.event = Event::CloseFailed;
    close.cause = Cause::TooLong;
    close.limitMs = 90000;
    text = alertFor(close, "closing");
    TEST_ASSERT_EQUAL_STRING("Roof didn't close", text.title.c_str());
    TEST_ASSERT_NOT_NULL(strstr(text.message.c_str(), "within 90 s"));
    close.cause = Cause::Fault;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(close, "Park interlock: park switch 1 open").message.c_str(), "park switch 1 open"));
    close.cause = Cause::Refused;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(close, "Supply 10.9 V < 11.0 V").message.c_str(), "10.9 V < 11.0 V"));
    close.cause = Cause::Interrupted;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(close, "stopped").message.c_str(), "stopped before"));

    Decision cannot;
    cannot.event = Event::CannotClose;
    cannot.cause = Cause::PositionUnknown;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(cannot, "").message.c_str(), "position is unknown"));
    cannot.cause = Cause::FaultLatched;
    TEST_ASSERT_NOT_NULL(strstr(alertFor(cannot, "Timeout 61.0 s > 60.0 s").message.c_str(), "61.0 s"));

    Decision held;
    held.event = Event::StoppedByPerson;
    TEST_ASSERT_EQUAL_STRING("Automatic close stopped", alertFor(held, "").title.c_str());
    held.reminder = true;
    TEST_ASSERT_EQUAL_STRING("Still: Automatic close stopped", alertFor(held, "").title.c_str());
}

void test_reason_for_each_cause()
{
    Reasons reasons;
    reasons.park = "not parked: N.I.N.A. unreachable";
    reasons.fault = "Supply 10.9 V < 11.0 V";
    reasons.refusal = "Hall reading not settled";
    reasons.roof = State::Stopped;
    Decision d;
    d.cause = Cause::ParkTimeout;
    TEST_ASSERT_EQUAL_STRING(reasons.park.c_str(), reasonFor(d, reasons).c_str());
    d.cause = Cause::NinaNotConfigured;
    TEST_ASSERT_EQUAL_STRING(reasons.park.c_str(), reasonFor(d, reasons).c_str());
    d.cause = Cause::Fault;
    TEST_ASSERT_EQUAL_STRING(reasons.fault.c_str(), reasonFor(d, reasons).c_str());
    d.cause = Cause::FaultLatched;
    TEST_ASSERT_EQUAL_STRING(reasons.fault.c_str(), reasonFor(d, reasons).c_str());
    d.cause = Cause::Refused;
    TEST_ASSERT_EQUAL_STRING(reasons.refusal.c_str(), reasonFor(d, reasons).c_str());
    d.cause = Cause::Interrupted;
    TEST_ASSERT_EQUAL_STRING("stopped", reasonFor(d, reasons).c_str());
    d.cause = Cause::TooLong;
    TEST_ASSERT_EQUAL_STRING("stopped", reasonFor(d, reasons).c_str());
    d.cause = Cause::PositionUnknown;
    TEST_ASSERT_EQUAL_STRING("", reasonFor(d, reasons).c_str());
}

void test_critical_events()
{
    TEST_ASSERT_TRUE(isCritical(Event::ParkNotConfirmed));
    TEST_ASSERT_TRUE(isCritical(Event::CloseFailed));
    TEST_ASSERT_TRUE(isCritical(Event::CannotClose));
    TEST_ASSERT_TRUE(isCritical(Event::StoppedByPerson));
    TEST_ASSERT_FALSE(isCritical(Event::None));
    TEST_ASSERT_FALSE(isCritical(Event::Started));
    TEST_ASSERT_FALSE(isCritical(Event::EndedSafe));
    TEST_ASSERT_FALSE(isCritical(Event::Closed));
}

void test_names()
{
    TEST_ASSERT_EQUAL_STRING("waitPark", phaseName(Phase::WaitPark));
    TEST_ASSERT_EQUAL_STRING("cannotMove", phaseName(Phase::CannotMove));
    TEST_ASSERT_EQUAL_STRING("stoppedByPerson", eventName(Event::StoppedByPerson));
    TEST_ASSERT_EQUAL_STRING("faultLatched", causeName(Cause::FaultLatched));
    TEST_ASSERT_EQUAL_STRING("?", phaseName(static_cast<Phase>(99)));
}

void test_only_a_safe_verdict_is_safe()
{
    Inputs in;
    Safety::ParkResult park;
    applySafety(in, Safety::Verdict::Safe, park);
    TEST_ASSERT_TRUE(in.weatherSafe);
    applySafety(in, Safety::Verdict::Unknown, park);
    TEST_ASSERT_FALSE(in.weatherSafe);
    applySafety(in, Safety::Verdict::Unsafe, park);
    TEST_ASSERT_FALSE(in.weatherSafe);
}

void test_park_mapping()
{
    Inputs in;
    Safety::ParkResult park;
    park.parked = true;
    applySafety(in, Safety::Verdict::Unsafe, park);
    TEST_ASSERT_TRUE(in.parked);
    TEST_ASSERT_TRUE(in.ninaConfigured);
    park.parked = false;
    park.reasons = Safety::ParkNinaNotConfigured | Safety::ParkSwitch1Open;
    applySafety(in, Safety::Verdict::Unsafe, park);
    TEST_ASSERT_FALSE(in.parked);
    TEST_ASSERT_FALSE(in.ninaConfigured);
    park.reasons = Safety::ParkNinaUnreachable;
    applySafety(in, Safety::Verdict::Unsafe, park);
    TEST_ASSERT_TRUE(in.ninaConfigured);
}

void test_roof_mapping()
{
    Inputs in;
    Roof::Status status;
    status.state = State::Closing;
    status.moveTimeoutMs = 61000;
    applyRoof(in, status, true);
    TEST_ASSERT_EQUAL(State::Closing, in.roof);
    TEST_ASSERT_EQUAL_UINT32(61000, in.moveTimeoutMs);
    TEST_ASSERT_TRUE(in.closePending);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_alert_text_names_the_step_and_the_reason);
    RUN_TEST(test_reason_for_each_cause);
    RUN_TEST(test_critical_events);
    RUN_TEST(test_names);
    RUN_TEST(test_only_a_safe_verdict_is_safe);
    RUN_TEST(test_park_mapping);
    RUN_TEST(test_roof_mapping);
    return UNITY_END();
}
