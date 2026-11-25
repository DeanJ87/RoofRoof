// Spec 019: the teach (lib/CalibrationLogic, Teach.cpp, research R1-R7): the
// jog's start checks and renewals, the reading window, the marks, the
// proposal, the confirm check, and the document and request bodies. Stepped in
// RoofControl's order: the gates, the tick, the teach's window.

#include <ArduinoJson.h>
#include <string>
#include <unity.h>

#include "RoofController.h"
#include "RoofGates.h"
#include "RoofPlant.h"
#include "RoofText.h"
#include "Teach.h"

using namespace SQM;
using namespace SQM::Calib;
using Roof::End;
using Roof::FaultKind;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t STEP_MS = 20;

    struct Rig
    {
        Roof::Controller roof;
        RoofPlant::Plant plant;
        Teach teach;
        RoofIo::GateInputs gates;
        uint32_t nowMs = 1000;

        explicit Rig(float startMm = 0.0f)
        {
            plant.positionMm = startMm;
            gates.weather = Safety::Verdict::Safe;
            gates.weatherText = "safe";
            gates.parked = true;
            gates.parkText = "parked";
            steps(1200);
        }

        void step()
        {
            plant.drive(roof.output(), STEP_MS);
            nowMs += STEP_MS;
            if (RoofIo::interlockLost(roof.status(), gates))
                roof.raiseFault(Roof::Fault{FaultKind::External, 1.0f, 0.0f});
            if (RoofIo::openMustStop(roof.status(), gates))
                roof.command(Roof::Command::Stop, Roof::Origin::Automatic);
            const Roof::Readings r = plant.readings(nowMs);
            roof.tick(r);
            teach.step(roof.status(), r.hallCounts);
        }

        void steps(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += STEP_MS)
                step();
        }

        StartInputs in() const
        {
            StartInputs s;
            s.nowMs = nowMs;
            s.gates = gates;
            s.faultText = "fault";
            return s;
        }

        Answer jog(End target, bool attended = true, bool calibrating = false)
        {
            return teach.jog(roof, target, attended, calibrating, in());
        }

        // Holds the jog for `ms`, renewing every 140 ms; then releases it (a Stop).
        void hold(uint32_t ms)
        {
            for (uint32_t t = 0; t < ms; t += STEP_MS)
            {
                if (t % 140 == 0)
                    Teach::hold(roof, roof.jogId());
                step();
            }
            roof.command(Roof::Command::Stop, Roof::Origin::Person);
        }

        Answer mark(Where where) { return teach.mark(where, roof.status(), false); }
    };

    void assertRefused(const char *reason, const Answer &answer)
    {
        TEST_ASSERT_FALSE(answer.accepted);
        TEST_ASSERT_EQUAL_STRING(reason, answer.reason.c_str());
        TEST_ASSERT_FALSE(answer.text.empty());
    }

    Stats readings(uint16_t value, uint16_t spread = 0, uint32_t count = 50)
    {
        Stats s;
        for (uint32_t i = 0; i < count; ++i)
            s.add(static_cast<uint16_t>(value - spread + (i % 2) * 2 * spread));
        return s;
    }
} // namespace

// --- The jog's start checks -----------------------------------------------------

void test_a_jog_needs_the_attended_confirmation(void)
{
    Rig r;
    assertRefused("notAttended", r.jog(End::Open, false));
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    TEST_ASSERT_TRUE(r.roof.status().jogging);
}

void test_both_directions_need_the_safe_weather_no_daylight_and_the_park(void)
{
    for (End target : {End::Open, End::Closed})
    {
        Rig unsafe(900.0f);
        unsafe.gates.weather = Safety::Verdict::Unsafe;
        assertRefused("weatherNotSafe", unsafe.jog(target));
        Rig daylight(900.0f);
        daylight.gates.daylightCloses = true;
        assertRefused("daylight", daylight.jog(target));
        Rig unparked(900.0f);
        unparked.gates.parked = false;
        assertRefused("mountNotParked", unparked.jog(target));
        Rig switchOpen(900.0f);
        switchOpen.gates.fittedSwitches = 1;
        switchOpen.gates.switch1 = RoofIo::Contact::Open;
        assertRefused("mountNotParked", switchOpen.jog(target));
        TEST_ASSERT_FALSE(switchOpen.roof.status().jogging);
    }
}

void test_a_jog_is_refused_while_anything_else_is_going_on(void)
{
    Rig r(900.0f);
    assertRefused("calibrating", r.jog(End::Open, true, true));
    StartInputs s = r.in();
    s.sequenceIdle = false;
    assertRefused("closeSequence", r.teach.jog(r.roof, End::Open, true, false, s));
    s = r.in();
    s.closePending = true;
    assertRefused("closeWaiting", r.teach.jog(r.roof, End::Open, true, false, s));
    s = r.in();
    s.settingsPending = true;
    assertRefused("settingsPending", r.teach.jog(r.roof, End::Open, true, false, s));
    s = r.in();
    s.busy = true;
    s.busyReason = "hotspot";
    s.busyText = "The setup hotspot is on";
    assertRefused("hotspot", r.teach.jog(r.roof, End::Open, true, false, s));
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    r.steps(40);
    assertRefused("moving", r.jog(End::Closed));
    r.roof.raiseFault(Roof::Fault{FaultKind::External, 0, 0});
    assertRefused("faultLatched", r.jog(End::Closed));
}

// Spec 033: the attended checks leave a Hall rail fault to the controller,
// which runs a person's jog blind; any other fault still refuses it.
void test_a_jog_runs_with_the_hall_rail_fault_only(void)
{
    Rig r(900.0f);
    r.plant.railCounts = 4095;
    r.steps(200);
    TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::HallRail);
    assertRefused("notAttended", r.jog(End::Closed, false));
    TEST_ASSERT_TRUE(r.jog(End::Closed).accepted);
    TEST_ASSERT_TRUE(r.roof.status().jogging);
    TEST_ASSERT_TRUE(Teach::yieldToClose(r.roof));
    TEST_ASSERT_FALSE(r.roof.status().jogging);

    Rig other(900.0f);
    other.roof.raiseFault(Roof::Fault{FaultKind::External, 0, 0});
    other.plant.railCounts = 4095;
    other.steps(200);
    assertRefused("faultLatched", other.jog(End::Closed));
}

void test_the_controllers_own_refusals_come_through(void)
{
    Rig r(900.0f);
    r.plant.supplyVolts = 10.0f;
    r.steps(40);
    assertRefused("supplyLow", r.jog(End::Open));
}

// --- Renewals and the gates while jogging ----------------------------------------

void test_a_renewal_needs_the_running_jog(void)
{
    Rig r(900.0f);
    assertRefused("jogEnded", Teach::hold(r.roof, 1));
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    TEST_ASSERT_TRUE(Teach::hold(r.roof, r.roof.jogId()).accepted);
    assertRefused("jogEnded", Teach::hold(r.roof, r.roof.jogId() + 1));
    r.steps(Roof::JOG_HOLD_MS + 40); // not renewed: stopped
    TEST_ASSERT_FALSE(r.roof.status().jogging);
    assertRefused("jogEnded", Teach::hold(r.roof, r.roof.jogId()));
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
}

void test_weather_or_daylight_turning_stops_a_jog_either_way(void)
{
    for (End target : {End::Open, End::Closed})
    {
        Rig r(900.0f);
        TEST_ASSERT_TRUE(r.jog(target).accepted);
        for (int i = 0; i < 20; ++i)
        {
            Teach::hold(r.roof, r.roof.jogId());
            r.step();
        }
        TEST_ASSERT_TRUE(r.roof.status().driving);
        r.gates.weather = Safety::Verdict::Unsafe;
        Teach::hold(r.roof, r.roof.jogId());
        r.step();
        TEST_ASSERT_FALSE(r.roof.status().jogging);
        TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
        TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::None);

        Rig d(900.0f);
        TEST_ASSERT_TRUE(d.jog(target).accepted);
        d.steps(100);
        d.gates.daylightCloses = true;
        d.step();
        TEST_ASSERT_FALSE(d.roof.status().jogging);
    }
}

void test_the_park_lost_while_jogging_is_a_fault_either_way(void)
{
    for (End target : {End::Open, End::Closed})
    {
        Rig r(900.0f);
        TEST_ASSERT_TRUE(r.jog(target).accepted);
        r.steps(100);
        r.gates.parked = false;
        r.step();
        TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::External);
        TEST_ASSERT_FALSE(r.roof.status().jogging);
        TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
    }
}

// --- The window and the marks ----------------------------------------------------

void test_a_mark_needs_a_second_at_rest(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    assertRefused("notAtRest", r.mark(Where::Closed));
    r.hold(2000);
    r.steps(20); // just stopped
    assertRefused("notAtRest", r.mark(Where::PartWay));
    r.steps(Calib::REST_READINGS * STEP_MS - 60);
    assertRefused("notAtRest", r.mark(Where::PartWay));
    r.steps(40);
    TEST_ASSERT_TRUE(r.mark(Where::PartWay).accepted);
    TEST_ASSERT_FALSE(r.teach.report(r.roof).marked[0]);
    TEST_ASSERT_TRUE(r.teach.report(r.roof).marked[2]);
    TEST_ASSERT_EQUAL_UINT32(REST_READINGS, r.teach.report(r.roof).marks[2].count);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(r.plant.restingCounts), static_cast<float>(r.teach.report(r.roof).marks[2].mean()));
    assertRefused("calibrating", r.teach.mark(Where::Closed, r.roof.status(), true));
}

void test_a_mark_needs_a_settled_hall_reading(void)
{
    Rig r;
    r.plant.railCounts = 4090;
    r.steps(200);
    assertRefused("notAtRest", r.mark(Where::Closed)); // rail readings empty the window
    TEST_ASSERT_FALSE(r.teach.report(r.roof).restReady);
}

void test_teaching_the_three_places_proposes_thresholds_that_read_the_magnets(void)
{
    Rig r;
    TEST_ASSERT_TRUE(r.mark(Where::Closed).accepted);
    TEST_ASSERT_FALSE(r.teach.proposal().made);
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    r.hold(20000); // about 300 mm
    r.steps(1200);
    TEST_ASSERT_TRUE(r.mark(Where::PartWay).accepted);
    for (int i = 0; i < 6 && !r.plant.atOpenMagnet(); ++i)
    {
        TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
        r.hold(25000);
        r.steps(600);
        TEST_ASSERT_TRUE(r.roof.status().fault.kind == FaultKind::None);
    }
    TEST_ASSERT_TRUE(r.plant.atOpenMagnet());
    r.steps(1200);
    TEST_ASSERT_TRUE(r.mark(Where::Open).accepted);
    const TeachProposal p = r.teach.proposal();
    TEST_ASSERT_TRUE(p.made);
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_TRUE(p.highEnd == End::Open);
    TEST_ASSERT_EQUAL_UINT16((r.plant.restingCounts + r.plant.highMagnetCounts + 1) / 2, p.highCounts);
    TEST_ASSERT_EQUAL_UINT16((r.plant.restingCounts + r.plant.lowMagnetCounts + 1) / 2, p.lowCounts);
    TEST_ASSERT_TRUE(r.teach.checkConfirm(p.highEnd, p.highCounts, p.lowCounts).accepted);
    // The taught values read the magnets once saved: the roof is known open.
    Roof::Settings taught = r.roof.settings();
    taught.hallHighEnd = p.highEnd;
    taught.hallHighCounts = p.highCounts;
    taught.hallLowCounts = p.lowCounts;
    TEST_ASSERT_TRUE(r.roof.applySettings(taught).accepted);
    r.steps(100);
    TEST_ASSERT_TRUE(r.roof.status().state == Roof::State::Open);
    TEST_ASSERT_TRUE(r.roof.status().positionKnown);
}

// --- The proposal -----------------------------------------------------------------

void test_the_halfway_points_are_proposed_when_they_fit(void)
{
    const TeachProposal p = proposeTeach(readings(700), readings(3400), readings(2050));
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_TRUE(p.highEnd == End::Open);
    TEST_ASSERT_EQUAL_UINT16(2725, p.highCounts);
    TEST_ASSERT_EQUAL_UINT16(1375, p.lowCounts);
    const TeachProposal flipped = proposeTeach(readings(3400), readings(700), readings(2050));
    TEST_ASSERT_TRUE(flipped.highEnd == End::Closed);
    TEST_ASSERT_EQUAL_UINT16(2725, flipped.highCounts);
}

void test_a_halfway_point_outside_the_window_is_moved_into_it(void)
{
    // A noisy part-way reading: the clearance (4 sigma = 200) pushes the low
    // threshold below the halfway point of 1875.
    const TeachProposal p = proposeTeach(readings(1700), readings(3400), readings(2050, 50));
    TEST_ASSERT_TRUE(p.ok);
    TEST_ASSERT_EQUAL_UINT16(1800, p.lowCounts); // 2000 - 200
    TEST_ASSERT_TRUE(p.highCounts >= 2100 + 200);
    TEST_ASSERT_TRUE(p.highCounts >= p.lowCounts + RoofConfig::MIN_HALL_GAP_COUNTS);
    // An end with noise at rest keeps its margin.
    const TeachProposal q = proposeTeach(readings(700, 20), readings(2420, 5), readings(2050));
    TEST_ASSERT_TRUE(q.ok);
    TEST_ASSERT_TRUE(q.highCounts <= 2415 - 15);
    TEST_ASSERT_TRUE(q.highCounts >= 2150);
}

void test_ends_that_cant_be_told_apart_propose_nothing(void)
{
    const TeachProposal same = proposeTeach(readings(3300), readings(3400), readings(2050));
    TEST_ASSERT_TRUE(same.made);
    TEST_ASSERT_FALSE(same.ok);
    TEST_ASSERT_EQUAL_STRING("samePole", same.problem.reason.c_str());
    const TeachProposal weak = proposeTeach(readings(1800), readings(3400), readings(2050));
    TEST_ASSERT_EQUAL_STRING("weakEnd", weak.problem.reason.c_str());
    // The part-way reading overlaps an end: no threshold clears it.
    const TeachProposal overlap = proposeTeach(readings(700), readings(3400), readings(2050, 600));
    TEST_ASSERT_FALSE(overlap.ok);
    TEST_ASSERT_EQUAL_STRING("noRoom", overlap.problem.reason.c_str());
}

void test_confirm_needs_the_current_proposal(void)
{
    Rig r(900.0f);
    assertRefused("noProposal", r.teach.checkConfirm(End::Open, 2725, 1375));
    TEST_ASSERT_TRUE(r.mark(Where::Closed).accepted); // all three at the same place
    TEST_ASSERT_TRUE(r.mark(Where::Open).accepted);
    TEST_ASSERT_TRUE(r.mark(Where::PartWay).accepted);
    TEST_ASSERT_TRUE(r.teach.proposal().made);
    assertRefused("noProposal", r.teach.checkConfirm(End::Open, 2725, 1375)); // samePole's text
    r.teach.clear();
    TEST_ASSERT_FALSE(r.teach.proposal().made);
    for (bool m : r.teach.report(r.roof).marked)
        TEST_ASSERT_FALSE(m);
}

void test_confirm_refuses_values_that_arent_the_proposal(void)
{
    Teach t;
    Roof::Status still;
    still.hall = Roof::HallLevel::Low;
    for (uint16_t c : {700, 3400, 2050})
    {
        for (int i = 0; i < REST_READINGS; ++i)
            t.step(still, c);
        TEST_ASSERT_TRUE(t.mark(c == 700 ? Where::Closed : (c == 3400 ? Where::Open : Where::PartWay), still, false).accepted);
    }
    TEST_ASSERT_TRUE(t.checkConfirm(End::Open, 2725, 1375).accepted);
    assertRefused("proposalChanged", t.checkConfirm(End::Closed, 2725, 1375));
    assertRefused("proposalChanged", t.checkConfirm(End::Open, 2726, 1375));
    assertRefused("proposalChanged", t.checkConfirm(End::Open, 2725, 1374));
}

// --- The document and the request bodies -------------------------------------------

void test_the_document(void)
{
    Rig r(900.0f);
    TEST_ASSERT_TRUE(r.mark(Where::PartWay).accepted);
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    r.steps(100);
    DynamicJsonDocument doc(TEACH_DOCUMENT_CAPACITY);
    writeTeachDocument(doc.to<JsonObject>(), r.teach.report(r.roof));
    TEST_ASSERT_FALSE(doc.overflowed());
    TEST_ASSERT_TRUE(doc["jog"]["active"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("open", doc["jog"]["direction"]);
    TEST_ASSERT_EQUAL_UINT32(r.roof.jogId(), doc["jog"]["id"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(Roof::JOG_HOLD_MS, doc["jog"]["holdMs"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(Roof::JOG_KEEPALIVE_MS, doc["jog"]["keepAliveMs"].as<uint32_t>());
    TEST_ASSERT_EQUAL_UINT32(Roof::JOG_MAX_MS, doc["jog"]["limitMs"].as<uint32_t>());
    TEST_ASSERT_FALSE(doc["rest"]["ready"].as<bool>());
    TEST_ASSERT_TRUE(doc["rest"]["counts"].isNull());
    TEST_ASSERT_TRUE(doc["marks"]["closed"].isNull());
    TEST_ASSERT_EQUAL_UINT32(50, doc["marks"]["partWay"]["count"].as<uint32_t>());
    TEST_ASSERT_TRUE(doc["proposal"].isNull());

    Teach t;
    Roof::Status still;
    still.hall = Roof::HallLevel::Low;
    for (uint16_t c : {3300, 3400, 2050})
    {
        for (int i = 0; i < REST_READINGS; ++i)
            t.step(still, c);
        t.mark(c == 3300 ? Where::Closed : (c == 3400 ? Where::Open : Where::PartWay), still, false);
    }
    Roof::Controller idle;
    DynamicJsonDocument bad(TEACH_DOCUMENT_CAPACITY);
    writeTeachDocument(bad.to<JsonObject>(), t.report(idle));
    TEST_ASSERT_FALSE(bad.overflowed());
    TEST_ASSERT_TRUE(bad["jog"]["direction"].isNull());
    TEST_ASSERT_TRUE(bad["rest"]["ready"].as<bool>());
    TEST_ASSERT_FALSE(bad["proposal"]["ok"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("samePole", bad["proposal"]["reason"]);
}

void test_the_request_bodies(void)
{
    StaticJsonDocument<256> doc;
    const char *error = nullptr;
    JogRequest jog;
    deserializeJson(doc, R"({"direction":"close","attended":true})");
    TEST_ASSERT_TRUE(parseJog(doc.as<JsonVariantConst>(), jog, error));
    TEST_ASSERT_TRUE(jog.target == End::Closed);
    TEST_ASSERT_TRUE(jog.attended);
    TEST_ASSERT_EQUAL_UINT32(0, jog.id);
    deserializeJson(doc, R"({"direction":"open","attended":false,"id":7})");
    TEST_ASSERT_TRUE(parseJog(doc.as<JsonVariantConst>(), jog, error));
    TEST_ASSERT_TRUE(jog.target == End::Open);
    TEST_ASSERT_EQUAL_UINT32(7, jog.id);
    for (const char *body :
         {R"({"direction":"closed","attended":true})",
          R"({"direction":"open"})",
          R"({"direction":"open","attended":true,"id":0})",
          R"({"direction":"open","attended":true,"id":"7"})"})
    {
        deserializeJson(doc, body);
        TEST_ASSERT_FALSE_MESSAGE(parseJog(doc.as<JsonVariantConst>(), jog, error), body);
    }
    Where at = Where::Closed;
    deserializeJson(doc, R"({"at":"partWay"})");
    TEST_ASSERT_TRUE(parseMark(doc.as<JsonVariantConst>(), at, error));
    TEST_ASSERT_TRUE(at == Where::PartWay);
    deserializeJson(doc, R"({"at":"middle"})");
    TEST_ASSERT_FALSE(parseMark(doc.as<JsonVariantConst>(), at, error));
    TeachValues values;
    deserializeJson(doc, R"({"hallHighEnd":"closed","hallHighCounts":2725,"hallLowCounts":1375})");
    TEST_ASSERT_TRUE(parseConfirm(doc.as<JsonVariantConst>(), values, error));
    TEST_ASSERT_TRUE(values.highEnd == End::Closed);
    TEST_ASSERT_EQUAL_UINT16(2725, values.highCounts);
    deserializeJson(doc, R"({"hallHighEnd":"open","hallHighCounts":-1,"hallLowCounts":1375})");
    TEST_ASSERT_FALSE(parseConfirm(doc.as<JsonVariantConst>(), values, error));
    deserializeJson(doc, R"({"hallHighEnd":"close","hallHighCounts":2725,"hallLowCounts":1375})");
    TEST_ASSERT_FALSE(parseConfirm(doc.as<JsonVariantConst>(), values, error));
}

// The release and the automatic close's take-over (src/RoofControl.cpp and the demo call these).
void test_release_and_the_automatic_close_end_a_jog(void)
{
    Rig r(900.0f);
    assertRefused("jogEnded", Teach::release(r.roof, 1));
    TEST_ASSERT_FALSE(Teach::yieldToClose(r.roof));
    TEST_ASSERT_TRUE(r.jog(End::Open).accepted);
    r.steps(100);
    assertRefused("jogEnded", Teach::release(r.roof, r.roof.jogId() + 1));
    TEST_ASSERT_TRUE(Teach::release(r.roof, r.roof.jogId()).accepted);
    TEST_ASSERT_FALSE(r.roof.status().jogging);

    // The automatic close stops the jog, then makes its own checks: part-way the
    // position is unknown, so (spec 030, constitution 8.0.0) it closes slowly
    // from there, as a person's close would.
    TEST_ASSERT_TRUE(r.jog(End::Closed).accepted);
    r.steps(100);
    TEST_ASSERT_FALSE(r.roof.command(Roof::Command::Close, Roof::Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(r.roof.status().jogging); // a close never takes a jog over
    TEST_ASSERT_TRUE(Teach::yieldToClose(r.roof));
    TEST_ASSERT_FALSE(r.roof.status().jogging);
    TEST_ASSERT_TRUE(r.roof.output().command == Driver::Command::Off);
    TEST_ASSERT_TRUE(r.roof.command(Roof::Command::Close, Roof::Origin::Automatic).accepted);
    TEST_ASSERT_TRUE(r.roof.status().fromUnknown);
    TEST_ASSERT_FALSE(r.roof.command(Roof::Command::Open, Roof::Origin::Automatic).accepted);
}

void test_a_release_body(void)
{
    StaticJsonDocument<256> doc;
    const char *error = nullptr;
    JogRequest jog;
    deserializeJson(doc, R"({"direction":"open","attended":true,"id":7,"release":true})");
    TEST_ASSERT_TRUE(parseJog(doc.as<JsonVariantConst>(), jog, error));
    TEST_ASSERT_TRUE(jog.release);
    TEST_ASSERT_EQUAL_UINT32(7, jog.id);
    deserializeJson(doc, R"({"id":9,"release":true})"); // a release needs only the id
    TEST_ASSERT_TRUE(parseJog(doc.as<JsonVariantConst>(), jog, error));
    TEST_ASSERT_EQUAL_UINT32(9, jog.id);
    deserializeJson(doc, R"({"direction":"open","attended":true,"release":true})");
    TEST_ASSERT_FALSE(parseJog(doc.as<JsonVariantConst>(), jog, error));
    deserializeJson(doc, R"({"direction":"open","attended":true,"id":7,"release":1})");
    TEST_ASSERT_FALSE(parseJog(doc.as<JsonVariantConst>(), jog, error));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_jog_needs_the_attended_confirmation);
    RUN_TEST(test_both_directions_need_the_safe_weather_no_daylight_and_the_park);
    RUN_TEST(test_a_jog_is_refused_while_anything_else_is_going_on);
    RUN_TEST(test_a_jog_runs_with_the_hall_rail_fault_only);
    RUN_TEST(test_the_controllers_own_refusals_come_through);
    RUN_TEST(test_a_renewal_needs_the_running_jog);
    RUN_TEST(test_weather_or_daylight_turning_stops_a_jog_either_way);
    RUN_TEST(test_the_park_lost_while_jogging_is_a_fault_either_way);
    RUN_TEST(test_a_mark_needs_a_second_at_rest);
    RUN_TEST(test_a_mark_needs_a_settled_hall_reading);
    RUN_TEST(test_teaching_the_three_places_proposes_thresholds_that_read_the_magnets);
    RUN_TEST(test_the_halfway_points_are_proposed_when_they_fit);
    RUN_TEST(test_a_halfway_point_outside_the_window_is_moved_into_it);
    RUN_TEST(test_ends_that_cant_be_told_apart_propose_nothing);
    RUN_TEST(test_confirm_needs_the_current_proposal);
    RUN_TEST(test_confirm_refuses_values_that_arent_the_proposal);
    RUN_TEST(test_the_document);
    RUN_TEST(test_the_request_bodies);
    RUN_TEST(test_release_and_the_automatic_close_end_a_jog);
    RUN_TEST(test_a_release_body);
    return UNITY_END();
}
