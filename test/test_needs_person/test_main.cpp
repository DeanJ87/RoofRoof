// Spec 030 (FR-007, User Story 3): the roof never sits silent. A restart with
// the position unknown or a fault latched, a restart with the roof open, a Hall
// sensor failing at any time and the close sequence unable to move each raise
// the critical needs_person alert; the lasting ones are reminded until a
// person has acted. Each text says what is wrong, with the measured value, and
// what a person must do (constitution III).

#include <unity.h>

#include <string>

#include "../close_rig/CloseRig.h"
#include "Config.h" // for the library finder: RoofLogic builds with ConfigModel
#include "NeedsPerson.h"
#include "RoofAlerts.h"

using namespace SQM;
using namespace SQM::CloseSeq;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint64_t REMIND = 1800000;

    RoofIo::Snapshot between()
    {
        RoofIo::Snapshot roof;
        roof.status.state = Roof::State::Unknown;
        roof.status.positionKnown = false;
        roof.status.hall = Roof::HallLevel::Between;
        roof.status.hallCounts = 2050;
        return roof;
    }

    RoofIo::Snapshot closed()
    {
        RoofIo::Snapshot roof;
        roof.status.state = Roof::State::Closed;
        roof.status.positionKnown = true;
        roof.status.hall = Roof::HallLevel::Low;
        roof.status.hallCounts = 900;
        return roof;
    }

    RoofIo::Snapshot hallRail(bool atRest)
    {
        RoofIo::Snapshot roof;
        roof.status.state = Roof::State::Error;
        roof.status.hall = Roof::HallLevel::Rail;
        roof.status.hallCounts = 4095;
        roof.fault.fault = {Roof::FaultKind::HallRail, 4095.0f, 4000.0f};
        roof.fault.atRest = atRest;
        return roof;
    }

    struct Watch
    {
        NeedsPerson watcher;
        int alerts = 0;
        Alerts::Alert last;
        Phase phase = Phase::Idle;

        // Steps every second from `fromMs` to `toMs`, counting the alerts.
        void run(const RoofIo::Snapshot &roof, uint64_t fromMs, uint64_t toMs)
        {
            for (uint64_t t = fromMs; t <= toMs; t += 1000)
            {
                NeedsPersonInputs in;
                in.nowMs = t;
                in.roof = &roof;
                in.phase = phase;
                in.remindMs = REMIND;
                Alerts::Alert alert;
                if (watcher.step(in, alert))
                {
                    ++alerts;
                    last = alert;
                }
            }
        }
    };

    bool has(const std::string &text, const char *part)
    {
        return text.find(part) != std::string::npos;
    }
} // namespace

void test_a_restart_with_the_position_unknown_alerts_and_reminds_until_known(void)
{
    Watch w;
    RoofIo::Snapshot roof = between();
    roof.status.hall = Roof::HallLevel::Unsettled; // just after the start
    w.run(roof, 0, 2000);
    TEST_ASSERT_EQUAL(0, w.alerts);
    roof = between();
    w.run(roof, 3000, 3000);
    TEST_ASSERT_EQUAL(1, w.alerts);
    TEST_ASSERT_EQUAL(Alerts::AlertType::NeedsPerson, w.last.type);
    TEST_ASSERT_EQUAL(Alerts::AlertLevel::Wake, w.last.level);
    TEST_ASSERT_TRUE(Alerts::isCritical(w.last.type));
    TEST_ASSERT_EQUAL_STRING("Roof needs a person", w.last.title.c_str());
    TEST_ASSERT_TRUE(has(w.last.message, "restarted"));
    TEST_ASSERT_TRUE(has(w.last.message, "Position unknown"));
    TEST_ASSERT_TRUE(has(w.last.message, "between (2050 counts)"));
    TEST_ASSERT_TRUE(has(w.last.message, "teach the position"));
    TEST_ASSERT_TRUE(w.watcher.active());
    TEST_ASSERT_TRUE(has(w.watcher.text(), "Position unknown"));
    // Reminded every interval while it holds.
    w.run(roof, 4000, 3000 + 2 * REMIND);
    TEST_ASSERT_EQUAL(3, w.alerts);
    TEST_ASSERT_EQUAL_STRING("Still: Roof needs a person", w.last.title.c_str());
    // A person moved the roof to a magnet: it ends, no more reminders.
    w.run(closed(), 3000 + 2 * REMIND + 1000, 3000 + 5 * REMIND);
    TEST_ASSERT_EQUAL(3, w.alerts);
    TEST_ASSERT_FALSE(w.watcher.active());
    TEST_ASSERT_EQUAL_STRING("", w.watcher.text().c_str());
}

void test_a_restart_with_a_fault_latched_says_to_clear_it(void)
{
    Watch w;
    RoofIo::Snapshot roof = between();
    roof.status.state = Roof::State::Error;
    roof.fault.fault = {Roof::FaultKind::Timeout, 61.0f, 60.0f};
    w.run(roof, 0, 6000);
    TEST_ASSERT_EQUAL(1, w.alerts);
    TEST_ASSERT_TRUE(has(w.last.message, "Fault latched"));
    TEST_ASSERT_TRUE(has(w.last.message, "clear the fault"));
    // The fault cleared but the position is still unknown: still needs a person.
    w.run(between(), 7000, 8000);
    TEST_ASSERT_TRUE(w.watcher.active());
    TEST_ASSERT_TRUE(has(w.watcher.text(), "Position unknown"));
    w.run(closed(), 9000, 10000);
    TEST_ASSERT_FALSE(w.watcher.active());
}

void test_a_restart_with_the_hall_sensor_at_a_rail_waits_for_it_to_clear(void)
{
    Watch w;
    w.run(hallRail(true), 0, 1000);
    TEST_ASSERT_EQUAL(1, w.alerts); // a fault counts as settled at once
    TEST_ASSERT_TRUE(has(w.last.message, "Hall sensor fault"));
    TEST_ASSERT_TRUE(has(w.last.message, "4095 counts"));
    TEST_ASSERT_TRUE(has(w.last.message, "clears itself"));
    // Cleared at rest by itself, position read afresh at the closed magnet.
    w.run(closed(), 2000, 3000);
    TEST_ASSERT_FALSE(w.watcher.active());
    TEST_ASSERT_EQUAL(1, w.alerts);
}

void test_a_restart_with_the_roof_open_alerts_once(void)
{
    Watch w;
    RoofIo::Snapshot roof = closed();
    roof.status.state = Roof::State::Stopped;
    roof.status.hall = Roof::HallLevel::Between;
    roof.status.positionMm = 420.0f;
    w.run(roof, 0, 3 * REMIND);
    TEST_ASSERT_EQUAL(1, w.alerts);
    TEST_ASSERT_TRUE(has(w.last.message, "stopped at about 420 mm"));
    TEST_ASSERT_TRUE(has(w.last.message, "not closed"));
    TEST_ASSERT_FALSE(w.watcher.active()); // not a fault: nothing in the status
}

void test_a_restart_with_the_roof_closed_says_nothing(void)
{
    Watch w;
    w.run(closed(), 0, 3 * REMIND);
    TEST_ASSERT_EQUAL(0, w.alerts);
    TEST_ASSERT_FALSE(w.watcher.active());
}

void test_a_roof_controller_that_did_not_start_needs_a_person(void)
{
    NeedsPerson watcher;
    const RoofIo::Snapshot roof = between();
    NeedsPersonInputs in;
    in.nowMs = NEEDS_PERSON_BOOT_MS;
    in.roof = &roof;
    in.roofStarted = false;
    Alerts::Alert alert;
    TEST_ASSERT_TRUE(watcher.step(in, alert));
    TEST_ASSERT_TRUE(has(alert.message, "didn't start"));
}

void test_a_hall_rail_fault_at_any_time_alerts_at_once_and_reminds(void)
{
    Watch w;
    w.run(closed(), 0, 60000);
    TEST_ASSERT_EQUAL(0, w.alerts);
    w.run(hallRail(false), 61000, 61000);
    TEST_ASSERT_EQUAL(1, w.alerts);
    TEST_ASSERT_TRUE(has(w.last.message, "The Hall sensor failed"));
    TEST_ASSERT_TRUE(has(w.last.message, "check the Hall sensor") || has(w.last.message, "the Hall sensor and its cable"));
    TEST_ASSERT_TRUE(has(w.last.vars[0].second, "Hall sensor at a rail: 4095 counts"));
    w.run(hallRail(false), 62000, 61000 + REMIND);
    TEST_ASSERT_EQUAL(2, w.alerts);
    // A person cleared it: over.
    w.run(closed(), 62000 + REMIND, 61000 + 3 * REMIND);
    TEST_ASSERT_EQUAL(2, w.alerts);
    TEST_ASSERT_FALSE(w.watcher.active());
    // It fails again: at once again.
    w.run(hallRail(false), 62000 + 3 * REMIND, 62000 + 3 * REMIND);
    TEST_ASSERT_EQUAL(3, w.alerts);
}

// Spec 036: an end magnet read too early raises needs_person at once, with the
// measured progress and the limit, and is reminded while it stays latched.
void test_an_end_read_too_early_alerts_at_once_and_reminds(void)
{
    Watch w;
    w.run(closed(), 0, 60000);
    RoofIo::Snapshot early;
    early.status.state = Roof::State::Error;
    early.status.hall = Roof::HallLevel::High;
    early.status.hallCounts = 2152;
    early.fault.fault = {Roof::FaultKind::EndTooEarly, 33.0f, 80.0f};
    w.run(early, 61000, 61000);
    TEST_ASSERT_EQUAL(1, w.alerts);
    TEST_ASSERT_TRUE(has(w.last.message, "End too early: magnet or sensor problem"));
    TEST_ASSERT_TRUE(has(w.last.message, "33 percent of the travel < 80 percent"));
    TEST_ASSERT_TRUE(has(w.last.message, "clear the fault"));
    TEST_ASSERT_FALSE(has(w.last.message, "The Hall sensor failed"));
    TEST_ASSERT_TRUE(w.last.type == Alerts::AlertType::NeedsPerson);
    w.run(early, 62000, 61000 + REMIND);
    TEST_ASSERT_EQUAL(2, w.alerts);
}

void test_the_close_sequence_unable_to_move_shows_and_the_sequence_reminds(void)
{
    Watch w;
    RoofIo::Snapshot roof = between();
    w.phase = Phase::CannotMove;
    w.run(roof, 0, 3000);
    TEST_ASSERT_EQUAL(1, w.alerts); // the restart's own alert
    // While the sequence can't move it reminds itself: no second reminder here.
    w.run(roof, 4000, 3000 + 2 * REMIND);
    TEST_ASSERT_EQUAL(1, w.alerts);
    TEST_ASSERT_TRUE(w.watcher.active());
    // Unable to move without a condition of the watcher's own (a fault cleared
    // to a known position meanwhile, say): still shown, in words.
    Watch only;
    only.run(closed(), 0, 6000);
    only.phase = Phase::CannotMove;
    only.run(closed(), 7000, 7000);
    TEST_ASSERT_TRUE(only.watcher.active());
    TEST_ASSERT_TRUE(has(only.watcher.text(), "can't move the roof"));
}

// The field's silent "cannotMove": the sequence's CannotClose is now the
// critical needs_person alert, raised at once and reminded while it holds.
void test_the_sequence_unable_to_move_raises_needs_person_with_reminders(void)
{
    using namespace CloseTest;
    Rig rig;
    rig.in.roof = Roof::State::Error;
    rig.in.weatherSafe = false;
    Counts counts = rig.runTo(T0 + 2 * REMIND + 1000, 1000);
    TEST_ASSERT_EQUAL(Phase::CannotMove, rig.seq.phase());
    int alerts = 0;
    for (const Decision &d : counts.events)
    {
        Alerts::AlertType type = Alerts::AlertType::Test;
        TEST_ASSERT_TRUE(alertTypeFor(d.event, type));
        TEST_ASSERT_EQUAL(Alerts::AlertType::NeedsPerson, type);
        if (d.alert)
            ++alerts;
    }
    TEST_ASSERT_EQUAL(3, alerts); // the first and two reminders
    const Alerts::Alert alert = criticalAlert(counts.events.front(), "Timeout 61.0 s > 60.0 s");
    TEST_ASSERT_TRUE(has(alert.message, "Timeout 61.0 s > 60.0 s"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_restart_with_the_position_unknown_alerts_and_reminds_until_known);
    RUN_TEST(test_a_restart_with_a_fault_latched_says_to_clear_it);
    RUN_TEST(test_a_restart_with_the_hall_sensor_at_a_rail_waits_for_it_to_clear);
    RUN_TEST(test_a_restart_with_the_roof_open_alerts_once);
    RUN_TEST(test_a_restart_with_the_roof_closed_says_nothing);
    RUN_TEST(test_a_roof_controller_that_did_not_start_needs_a_person);
    RUN_TEST(test_a_hall_rail_fault_at_any_time_alerts_at_once_and_reminds);
    RUN_TEST(test_an_end_read_too_early_alerts_at_once_and_reminds);
    RUN_TEST(test_the_close_sequence_unable_to_move_shows_and_the_sequence_reminds);
    RUN_TEST(test_the_sequence_unable_to_move_raises_needs_person_with_reminders);
    return UNITY_END();
}
