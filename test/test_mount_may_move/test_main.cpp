// Spec 043 (constitution 10.2.0, II): the telescope may move (a slew, a park,
// an unpark) only with the roof at an end. RoofRoof never asks N.I.N.A. to park
// unless the roof is at rest on its closed or open magnet; part-open, moving,
// unknown or faulted with the mount not parked, it neither parks nor closes
// and raises the critical needs-person alert, reminded until it is resolved.

#include <cmath>
#include <string>
#include <unity.h>

#include "../close_rig/CloseRig.h"
#include "DomeBridge.h"
#include "MountMove.h"
#include "RoofAlerts.h"

using namespace SQM;
using namespace CloseTest;
using RoofIo::MountSwitch;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Roof::Status at(State state)
    {
        Roof::Status s;
        s.state = state;
        return s;
    }

    Roof::Status stoppedAt(float mm)
    {
        Roof::Status s = at(State::Stopped);
        s.positionKnown = true;
        s.positionMm = mm;
        return s;
    }

    const char *const OWNER_TEXT =
        "Roof part-open (42 %) with the mount not parked: moving the roof or the telescope would hit the beams. A person must check.";
} // namespace

// --- The predicate ---

void test_only_the_two_magnets_let_the_mount_move(void)
{
    TEST_ASSERT_TRUE(RoofIo::mountMayMove(at(State::Closed)));
    TEST_ASSERT_TRUE(RoofIo::mountMayMove(at(State::Open)));
    TEST_ASSERT_TRUE(RoofIo::parkRequestAllowed(at(State::Closed)));
    TEST_ASSERT_TRUE(RoofIo::parkRequestAllowed(at(State::Open)));
    for (State state : {State::Unknown, State::Opening, State::Closing, State::Stopped, State::Error})
    {
        TEST_ASSERT_FALSE(RoofIo::mountMayMove(at(state)));
        TEST_ASSERT_FALSE(RoofIo::mountMayMove(state));
        TEST_ASSERT_FALSE(RoofIo::parkRequestAllowed(at(state)));
    }
    // A jog, a calibration leg or the driver on never counts, whatever the state reads.
    Roof::Status jog = at(State::Closed);
    jog.jogging = true;
    TEST_ASSERT_FALSE(RoofIo::mountMayMove(jog));
    Roof::Status leg = at(State::Open);
    leg.calibrating = true;
    TEST_ASSERT_FALSE(RoofIo::mountMayMove(leg));
    Roof::Status driving = at(State::Open);
    driving.driving = true;
    TEST_ASSERT_FALSE(RoofIo::mountMayMove(driving));
}

void test_the_texts_name_where_the_roof_is(void)
{
    TEST_ASSERT_EQUAL_STRING(OWNER_TEXT, RoofIo::mountBlockedText(stoppedAt(840.0f), 2000).c_str());
    // Never 0 % or 100 % part-way (those read as an end).
    TEST_ASSERT_EQUAL_STRING("part-open (1 %)", RoofIo::roofPlaceText(stoppedAt(1.0f), 2000).c_str());
    TEST_ASSERT_EQUAL_STRING("part-open (99 %)", RoofIo::roofPlaceText(stoppedAt(1999.0f), 2000).c_str());
    TEST_ASSERT_EQUAL_STRING("position unknown", RoofIo::roofPlaceText(at(State::Unknown), 2000).c_str());
    TEST_ASSERT_EQUAL_STRING("moving", RoofIo::roofPlaceText(at(State::Opening), 2000).c_str());
    TEST_ASSERT_EQUAL_STRING("part-open, position unknown", RoofIo::roofPlaceText(at(State::Stopped), 2000).c_str());
    TEST_ASSERT_EQUAL_STRING("part-open, position unknown", RoofIo::roofPlaceText(stoppedAt(500.0f), 0).c_str());
    TEST_ASSERT_TRUE(RoofIo::mountBlockedText(at(State::Unknown), 2000).rfind("Roof position unknown with the mount not parked", 0) == 0);
}

// --- The Switch values ---

void test_the_switches_follow_the_roof(void)
{
    struct Row
    {
        State state;
        bool may, open, closed;
    };
    const Row rows[] = {
        {State::Closed, true, false, true},
        {State::Open, true, true, false},
        {State::Stopped, false, false, false},
        {State::Unknown, false, false, false},
        {State::Opening, false, false, false},
        {State::Closing, false, false, false},
        {State::Error, false, false, false},
    };
    for (const Row &row : rows)
    {
        const Roof::Status s = at(row.state);
        TEST_ASSERT_EQUAL(row.may, RoofIo::mountSwitchValue(MountSwitch::MountMayMove, s, true));
        TEST_ASSERT_EQUAL(row.open, RoofIo::mountSwitchValue(MountSwitch::RoofOpen, s, true));
        TEST_ASSERT_EQUAL(row.closed, RoofIo::mountSwitchValue(MountSwitch::RoofClosed, s, true));
        // The controller not running: everything false.
        TEST_ASSERT_FALSE(RoofIo::mountSwitchValue(MountSwitch::MountMayMove, s, false));
        TEST_ASSERT_FALSE(RoofIo::mountSwitchValue(MountSwitch::RoofOpen, s, false));
        TEST_ASSERT_FALSE(RoofIo::mountSwitchValue(MountSwitch::RoofClosed, s, false));
    }
}

void test_the_switch_readings_are_fixed_in_order_and_name(void)
{
    const std::vector<Alpaca::SwitchReading> r = RoofIo::switchReadings(at(State::Open), true);
    TEST_ASSERT_EQUAL(3, r.size());
    TEST_ASSERT_EQUAL_STRING("Mount may move", r[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Roof fully open", r[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Roof fully closed", r[2].name.c_str());
    TEST_ASSERT_TRUE(r[0].value);
    TEST_ASSERT_TRUE(r[1].value);
    TEST_ASSERT_FALSE(r[2].value);
    for (const Alpaca::SwitchReading &reading : r)
        TEST_ASSERT_FALSE(reading.description.empty());
    TEST_ASSERT_EQUAL(3, RoofIo::switchReadings(Roof::Status(), false).size());
}

// --- The automatic close never asks for a park between the ends ---

void test_stopped_part_way_unparked_no_park_request_and_a_person_is_needed(void)
{
    for (State state : {State::Stopped, State::Unknown})
    {
        Rig r;
        r.in.roof = state;
        r.in.parked = false;
        Counts c;
        Rig::note(c, r.startUnsafe());
        if (state == State::Unknown)
            Rig::note(c, r.at(T0 + UNKNOWN_CONFIRM_MS + 100)); // the Hall reading settles first
        const Counts more = r.runTo(T0 + 2 * REMIND_MS + 10000, 1000);
        TEST_ASSERT_EQUAL(0, c.requests + more.requests);
        TEST_ASSERT_EQUAL(0, c.closes + more.closes);
        TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase());
        // The needs-person event at once, then reminded.
        const Decision first = c.events.back();
        assertEvent(first, Event::CannotClose, Cause::RoofBetweenEnds);
        TEST_ASSERT_TRUE(first.alert);
        TEST_ASSERT_TRUE(more.events.size() >= 2);
        for (const Decision &d : more.events)
        {
            assertEvent(d, Event::CannotClose, Cause::RoofBetweenEnds);
            TEST_ASSERT_TRUE(d.reminder);
        }
    }
}

// Opening: armed, judged once it stops, never asking meanwhile.
void test_an_opening_roof_is_judged_once_it_stops(void)
{
    Rig r;
    r.in.roof = State::Opening;
    const Decision d = r.startUnsafe();
    TEST_ASSERT_EQUAL(Action::None, d.action);
    assertEvent(d, Event::Started, Cause::None);
    TEST_ASSERT_EQUAL(0, r.runTo(T0 + 30000, 100).requests);
    r.in.roof = State::Stopped; // the open stopped by itself (unsafe)
    assertEvent(r.at(T0 + 30100), Event::CannotClose, Cause::RoofBetweenEnds);
    r.in.roof = State::Open; // ...or reached the open magnet: asked at once
    TEST_ASSERT_EQUAL(Action::RequestPark, r.at(T0 + 30200).action);
}

void test_a_faulted_roof_with_a_blind_close_ready_is_not_at_an_end_either(void)
{
    Rig r;
    r.in.roof = State::Error;
    r.in.blindReady = true;
    const Decision d = r.startUnsafe();
    TEST_ASSERT_EQUAL(Action::None, d.action);
    assertEvent(d, Event::CannotClose, Cause::RoofBetweenEnds);
    TEST_ASSERT_EQUAL(0, r.runTo(T0 + 600000, 1000).requests);
    // Parked by a person: the blind close may run as before, still with no request.
    r.in.parked = true;
    int blind = 0;
    int requests = 0;
    for (uint64_t t = T0 + 600100; t <= T0 + 600000 + PARK_SETTLE_MS + 2000; t += 100)
    {
        const Decision step = r.at(t);
        blind += step.action == Action::BlindClose ? 1 : 0;
        requests += step.action == Action::RequestPark ? 1 : 0;
    }
    TEST_ASSERT_EQUAL(0, requests);
    TEST_ASSERT_TRUE(blind >= 1); // (the rig never answers the close, so the sequence tries again)
}

void test_at_either_magnet_the_park_is_asked_for(void)
{
    Rig open; // starts open
    TEST_ASSERT_EQUAL(Action::RequestPark, open.startUnsafe().action);
    // A daylight close of a roof at its open magnet asks too; closed has nothing to do.
    Rig closed;
    closed.in.roof = State::Closed;
    TEST_ASSERT_EQUAL(Action::None, closed.startUnsafe().action);
    TEST_ASSERT_EQUAL(Phase::Idle, closed.seq.phase());
}

void test_a_roof_stopped_while_waiting_stops_the_requests(void)
{
    Rig r;
    TEST_ASSERT_EQUAL(Action::RequestPark, r.startUnsafe().action);
    r.in.roof = State::Stopped; // a person's jog, a power cut and a restart between the magnets...
    const Decision d = r.at(T0 + 1000);
    assertEvent(d, Event::CannotClose, Cause::RoofBetweenEnds);
    const Counts c = r.runTo(T0 + 3600000, 1000);
    TEST_ASSERT_EQUAL(0, c.requests);
    TEST_ASSERT_EQUAL(0, c.closes);
}

void test_back_at_an_end_the_sequence_carries_on_at_once(void)
{
    Rig r;
    r.in.roof = State::Stopped;
    r.startUnsafe();
    TEST_ASSERT_EQUAL(Phase::CannotMove, r.seq.phase());
    // A person drives the roof to the open magnet: the park is asked for at once (nothing failed: no back-off).
    r.in.roof = State::Open;
    const Decision d = r.at(T0 + 5000);
    TEST_ASSERT_EQUAL(Action::RequestPark, d.action);
    TEST_ASSERT_EQUAL(Phase::WaitPark, r.seq.phase());
}

void test_parked_while_part_open_the_close_goes_ahead_without_a_request(void)
{
    Rig r;
    r.in.roof = State::Stopped;
    r.startUnsafe();
    r.in.parked = true;
    Counts c = r.runTo(T0 + 100 + PARK_SETTLE_MS + 200, 100);
    TEST_ASSERT_EQUAL(0, c.requests);
    TEST_ASSERT_EQUAL(1, c.closes);
}

void test_safe_again_the_hold_ends_and_unsafe_again_alerts_afresh(void)
{
    Rig r;
    r.in.roof = State::Stopped;
    r.startUnsafe();
    r.in.weatherSafe = true;
    r.at(T0 + 1000);
    TEST_ASSERT_EQUAL(Phase::Idle, r.seq.phase());
    r.in.weatherSafe = false;
    const Decision d = r.at(T0 + 2000);
    assertEvent(d, Event::CannotClose, Cause::RoofBetweenEnds);
}

void test_the_nina_lost_close_is_not_held_back(void)
{
    // The owner's N.I.N.A.-lost close (off by default) closes without the
    // park, as before; it never asks for one between the ends.
    Rig r;
    r.in.roof = State::Stopped;
    r.in.ninaLost = true; // N.I.N.A. lost long enough, every fitted switch closed (autoClose.ninaLostClose)
    const Decision d = r.startUnsafe();
    TEST_ASSERT_EQUAL(Action::None, d.action);
    assertEvent(d, Event::Started, Cause::None);
    const Decision next = r.at(T0 + 100);
    TEST_ASSERT_EQUAL(Action::Close, next.action);
    TEST_ASSERT_TRUE(next.ninaLost);
}

void test_the_alert_says_what_to_do(void)
{
    Rig r;
    r.in.roof = State::Stopped;
    const Decision d = r.startUnsafe();
    Reasons reasons;
    reasons.betweenEnds = RoofIo::mountBlockedText(stoppedAt(840.0f), 2000);
    const std::string why = reasonFor(d, reasons);
    TEST_ASSERT_EQUAL_STRING(OWNER_TEXT, why.c_str());
    const Alerts::Alert alert = criticalAlert(d, why);
    TEST_ASSERT_TRUE(alert.type == Alerts::AlertType::NeedsPerson);
    TEST_ASSERT_TRUE(alert.level == Alerts::AlertLevel::Wake);
    TEST_ASSERT_TRUE(alert.message.find(OWNER_TEXT) != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("Roof not at an end: mount not parked", alert.title.c_str());
    TEST_ASSERT_EQUAL_STRING("roofBetweenEnds", causeName(Cause::RoofBetweenEnds));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_only_the_two_magnets_let_the_mount_move);
    RUN_TEST(test_the_texts_name_where_the_roof_is);
    RUN_TEST(test_the_switches_follow_the_roof);
    RUN_TEST(test_the_switch_readings_are_fixed_in_order_and_name);
    RUN_TEST(test_stopped_part_way_unparked_no_park_request_and_a_person_is_needed);
    RUN_TEST(test_an_opening_roof_is_judged_once_it_stops);
    RUN_TEST(test_a_faulted_roof_with_a_blind_close_ready_is_not_at_an_end_either);
    RUN_TEST(test_at_either_magnet_the_park_is_asked_for);
    RUN_TEST(test_a_roof_stopped_while_waiting_stops_the_requests);
    RUN_TEST(test_back_at_an_end_the_sequence_carries_on_at_once);
    RUN_TEST(test_parked_while_part_open_the_close_goes_ahead_without_a_request);
    RUN_TEST(test_safe_again_the_hold_ends_and_unsafe_again_alerts_afresh);
    RUN_TEST(test_the_nina_lost_close_is_not_held_back);
    RUN_TEST(test_the_alert_says_what_to_do);
    return UNITY_END();
}
