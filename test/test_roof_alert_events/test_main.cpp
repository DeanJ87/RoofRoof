#include <unity.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "AlertEngine.h"

// The roof's non-critical alert events in the engine (spec 007 FR-006 to
// FR-012): rising edges, settle times, the start-up grace, the cooldown, an
// event switched off, the closes, the announced bits kept across a reset, and
// a randomised property test (SC-003).

using namespace SQM::Alerts;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    constexpr uint32_t GRACE = 60;
    constexpr uint32_t COOLDOWN = 300;

    AlertRules rules(uint32_t grace = GRACE, uint32_t cooldown = COOLDOWN)
    {
        AlertRules r;
        r.startupGraceSeconds = grace;
        r.cooldownSeconds = cooldown;
        return r;
    }

    struct Roof
    {
        bool fault = false;
        bool supply = false;
        bool source = false;
        bool closing = false;
        uint32_t closed = 0;
    };

    AlertInputs at(uint32_t now, const Roof &roof)
    {
        AlertInputs in;
        in.nowSeconds = now;
        in.roof.known = true;
        in.roof.faultLatched = roof.fault;
        in.roof.fault = roof.fault ? "Timeout 61.0 s > 60.0 s" : "";
        in.roof.supplyLow = roof.supply;
        in.roof.supply = roof.supply ? "Supply 10.9 V < 11.0 V" : "";
        in.roof.sourceLost = roof.source;
        in.roof.source = roof.source ? "SQMeter HTTP unreachable: connection refused" : "";
        in.roof.closing = roof.closing;
        in.roof.weather = "unsafe: rain";
        in.roof.closedCount = roof.closed;
        return in;
    }

    size_t count(const std::vector<Alert> &alerts, AlertType type)
    {
        size_t n = 0;
        for (const Alert &a : alerts)
            n += a.type == type ? 1 : 0;
        return n;
    }

    // Steps the engine once a second from `from` to `to` (inclusive) and
    // collects what it raised.
    std::vector<Alert> run(AlertEngine &engine, uint32_t from, uint32_t to, const Roof &roof, const AlertRules &r = rules())
    {
        std::vector<Alert> all;
        for (uint32_t t = from; t <= to; ++t)
        {
            std::vector<Alert> out = engine.update(at(t, roof), r);
            all.insert(all.end(), out.begin(), out.end());
        }
        return all;
    }

    std::string var(const Alert &alert, const char *name)
    {
        for (const auto &v : alert.vars)
            if (v.first == name)
                return v.second;
        return "<missing>";
    }

    // An engine past its grace, with nothing wrong.
    AlertEngine started()
    {
        AlertEngine engine;
        run(engine, 0, GRACE, Roof{});
        return engine;
    }
} // namespace

void test_names_and_critical(void)
{
    TEST_ASSERT_EQUAL_STRING("mount_not_parked", alertTypeName(AlertType::MountNotParked));
    TEST_ASSERT_EQUAL_STRING("roof_not_closed", alertTypeName(AlertType::RoofNotClosed));
    TEST_ASSERT_EQUAL_STRING("move_fault", alertTypeName(AlertType::MoveFault));
    TEST_ASSERT_EQUAL_STRING("supply_low", alertTypeName(AlertType::SupplyLow));
    TEST_ASSERT_EQUAL_STRING("unsafe_closing", alertTypeName(AlertType::UnsafeClosing));
    TEST_ASSERT_EQUAL_STRING("roof_closed", alertTypeName(AlertType::RoofClosed));
    TEST_ASSERT_EQUAL_STRING("safety_lost", alertTypeName(AlertType::SafetyLost));
    TEST_ASSERT_TRUE(isCritical(AlertType::MountNotParked));
    TEST_ASSERT_TRUE(isCritical(AlertType::RoofNotClosed));
    for (size_t i = 0; i < ALERT_TYPE_COUNT; ++i)
    {
        const AlertType type = static_cast<AlertType>(i);
        if (type != AlertType::MountNotParked && type != AlertType::RoofNotClosed)
            TEST_ASSERT_FALSE_MESSAGE(isCritical(type), alertTypeName(type));
    }
}

void test_fault_alerts_once_with_its_text(void)
{
    AlertEngine engine = started();
    Roof roof;
    roof.fault = true;
    std::vector<Alert> out = run(engine, 61, 61, roof);
    TEST_ASSERT_EQUAL(1, out.size());
    TEST_ASSERT_EQUAL(AlertType::MoveFault, out[0].type);
    TEST_ASSERT_EQUAL_STRING("Roof fault", out[0].title.c_str());
    TEST_ASSERT_NOT_NULL(strstr(out[0].message.c_str(), "Timeout 61.0 s > 60.0 s"));
    TEST_ASSERT_EQUAL_STRING("Timeout 61.0 s > 60.0 s", var(out[0], "reason").c_str());
    // Still latched: nothing more, however long.
    TEST_ASSERT_EQUAL(0, run(engine, 62, 4000, roof).size());
}

void test_fault_again_after_a_clear_waits_out_the_cooldown(void)
{
    AlertEngine engine = started();
    Roof faulted;
    faulted.fault = true;
    TEST_ASSERT_EQUAL(1, run(engine, 100, 100, faulted).size()); // alerted at 100
    run(engine, 101, 110, Roof{});                               // cleared
    // Latched again at 111: held until 100 + 300.
    TEST_ASSERT_EQUAL(0, run(engine, 111, 399, faulted).size());
    std::vector<Alert> out = run(engine, 400, 400, faulted);
    TEST_ASSERT_EQUAL(1, count(out, AlertType::MoveFault));
}

void test_a_held_fault_that_clears_is_not_sent(void)
{
    AlertEngine engine = started();
    Roof faulted;
    faulted.fault = true;
    run(engine, 100, 100, faulted);
    run(engine, 101, 101, Roof{});
    run(engine, 102, 200, faulted); // held by the cooldown
    TEST_ASSERT_EQUAL(0, run(engine, 201, 1000, Roof{}).size());
}

void test_supply_settles_for_30_seconds(void)
{
    AlertEngine engine = started();
    Roof low;
    low.supply = true;
    // Low from 100: 129 is 29 s, 130 is 30 s.
    TEST_ASSERT_EQUAL(0, run(engine, 100, 129, low).size());
    std::vector<Alert> out = run(engine, 130, 130, low);
    TEST_ASSERT_EQUAL(1, count(out, AlertType::SupplyLow));
    TEST_ASSERT_EQUAL_STRING("12 V supply low", out[0].title.c_str());
    TEST_ASSERT_NOT_NULL(strstr(out[0].message.c_str(), "Supply 10.9 V < 11.0 V"));
}

void test_supply_dip_shorter_than_the_settle_is_ignored(void)
{
    AlertEngine engine = started();
    Roof low;
    low.supply = true;
    run(engine, 100, 128, low); // 29 s
    run(engine, 129, 129, Roof{});
    TEST_ASSERT_EQUAL(0, run(engine, 130, 158, low).size()); // 29 s again
}

void test_source_settles_and_names_the_source(void)
{
    AlertEngine engine = started();
    Roof lost;
    lost.source = true;
    TEST_ASSERT_EQUAL(0, run(engine, 100, 129, lost).size());
    std::vector<Alert> out = run(engine, 130, 131, lost);
    TEST_ASSERT_EQUAL(1, out.size());
    TEST_ASSERT_EQUAL(AlertType::SafetyLost, out[0].type);
    TEST_ASSERT_NOT_NULL(strstr(out[0].message.c_str(), "SQMeter HTTP unreachable"));
}

void test_nothing_in_the_grace_then_a_present_condition_is_announced(void)
{
    AlertEngine engine;
    Roof bad;
    bad.fault = true;
    bad.supply = true;
    bad.source = true;
    TEST_ASSERT_EQUAL(0, run(engine, 0, 59, bad).size());
    std::vector<Alert> out = run(engine, 60, 60, bad);
    TEST_ASSERT_EQUAL(1, count(out, AlertType::MoveFault));
    TEST_ASSERT_EQUAL(1, count(out, AlertType::SupplyLow)); // held for 60 s already
    TEST_ASSERT_EQUAL(1, count(out, AlertType::SafetyLost));
}

void test_an_event_switched_off_follows_silently(void)
{
    AlertEngine engine = started();
    AlertRules off = rules();
    off.onMoveFault = false;
    Roof faulted;
    faulted.fault = true;
    TEST_ASSERT_EQUAL(0, run(engine, 100, 200, faulted, off).size());
    // Switched on while the fault stays latched: not announced (no change).
    TEST_ASSERT_EQUAL(0, run(engine, 201, 900, faulted).size());
    // A new fault after a clear is.
    run(engine, 901, 901, Roof{});
    TEST_ASSERT_EQUAL(1, run(engine, 902, 902, faulted).size());
}

void test_closing_once_per_cooldown_with_a_flapping_verdict(void)
{
    AlertEngine engine = started();
    Roof closing;
    closing.closing = true;
    std::vector<Alert> all;
    // Attempting for 10 s, idle for 10 s, for 10 minutes.
    for (uint32_t t = 100; t < 700; ++t)
    {
        const bool on = ((t - 100) / 10) % 2 == 0;
        std::vector<Alert> out = engine.update(at(t, on ? closing : Roof{}), rules());
        all.insert(all.end(), out.begin(), out.end());
    }
    TEST_ASSERT_EQUAL(2, count(all, AlertType::UnsafeClosing)); // at 100 and at 400
    TEST_ASSERT_EQUAL_STRING("unsafe: rain", var(all[0], "reason").c_str());
}

void test_closed_once_per_close_not_held_by_the_cooldown(void)
{
    AlertEngine engine = started();
    Roof roof;
    roof.closing = true;
    TEST_ASSERT_EQUAL(1, run(engine, 100, 100, roof).size());
    roof.closing = false;
    roof.closed = 1;
    std::vector<Alert> out = run(engine, 101, 200, roof);
    TEST_ASSERT_EQUAL(1, count(out, AlertType::RoofClosed));
    TEST_ASSERT_EQUAL_STRING("Roof closed after unsafe weather", out[0].title.c_str());
    TEST_ASSERT_NOT_NULL(strstr(out[0].message.c_str(), "unsafe: rain"));
    roof.closed = 2; // a second close within the cooldown
    TEST_ASSERT_EQUAL(1, count(run(engine, 201, 201, roof), AlertType::RoofClosed));
}

void test_closed_in_the_grace_is_sent_when_it_ends(void)
{
    AlertEngine engine;
    Roof roof;
    roof.closed = 1;
    TEST_ASSERT_EQUAL(0, run(engine, 0, 59, roof).size());
    TEST_ASSERT_EQUAL(1, count(run(engine, 60, 60, roof), AlertType::RoofClosed));
    TEST_ASSERT_EQUAL(0, run(engine, 61, 100, roof).size());
}

void test_closed_switched_off_is_dropped(void)
{
    AlertEngine engine = started();
    AlertRules off = rules();
    off.onRoofClosed = false;
    Roof roof;
    roof.closed = 1;
    TEST_ASSERT_EQUAL(0, run(engine, 100, 100, roof, off).size());
    TEST_ASSERT_EQUAL(0, run(engine, 101, 200, roof).size());
}

void test_unknown_roof_is_ignored(void)
{
    AlertEngine engine;
    AlertInputs in = at(100, Roof{true, true, true, true, 3});
    in.roof.known = false;
    TEST_ASSERT_EQUAL(0, engine.update(in, rules(0, 0)).size());
}

void test_announced_bits_survive_a_reset(void)
{
    AlertEngine before = started();
    Roof bad;
    bad.fault = true;
    bad.supply = true;
    run(before, 100, 140, bad);
    TEST_ASSERT_EQUAL_HEX8(0x03, before.announcedRoof());

    AlertEngine after;
    after.restoreAnnouncedRoof(before.announcedRoof());
    // The fault and supply are still there: not repeated after the grace.
    TEST_ASSERT_EQUAL(0, run(after, 0, 200, bad).size());
    // The supply recovers, then drops again: a new event.
    Roof faultOnly;
    faultOnly.fault = true;
    run(after, 201, 201, faultOnly);
    TEST_ASSERT_EQUAL_HEX8(0x01, after.announcedRoof());
    TEST_ASSERT_EQUAL(1, count(run(after, 202, 240, bad), AlertType::SupplyLow));
}

void test_a_restored_bit_for_a_cleared_condition_is_dropped(void)
{
    AlertEngine engine;
    engine.restoreAnnouncedRoof(0x07);
    run(engine, 0, 1, Roof{});
    TEST_ASSERT_EQUAL_HEX8(0x00, engine.announcedRoof());
}

void test_stacked_with_other_roof_events(void)
{
    AlertEngine engine = started();
    Roof roof;
    roof.fault = true;
    roof.closing = true;
    std::vector<Alert> out = run(engine, 100, 100, roof);
    TEST_ASSERT_EQUAL(2, out.size());
    const Alert stacked = stackAlerts(out);
    TEST_ASSERT_EQUAL(1, stacked.stacked.size());
    TEST_ASSERT_NOT_NULL(strstr(stacked.title.c_str(), "Roof fault"));
    TEST_ASSERT_NOT_NULL(strstr(stacked.title.c_str(), "Unsafe: closing"));
}

void test_waits_report_roof_settle_and_cooldown(void)
{
    AlertEngine engine = started();
    Roof low;
    low.supply = true;
    run(engine, 100, 110, low);
    std::vector<Wait> waits = engine.waits(110, rules());
    TEST_ASSERT_EQUAL(1, waits.size());
    TEST_ASSERT_EQUAL_STRING("roof:supply_low", waits[0].condition.c_str());
    TEST_ASSERT_EQUAL(WaitKind::Settle, waits[0].kind);
    TEST_ASSERT_EQUAL(20, waits[0].remainingSeconds);

    Roof faulted;
    faulted.fault = true;
    AlertEngine other = started();
    run(other, 100, 100, faulted);
    run(other, 101, 101, Roof{});
    run(other, 102, 150, faulted);
    waits = other.waits(150, rules());
    TEST_ASSERT_EQUAL(1, waits.size());
    TEST_ASSERT_EQUAL(WaitKind::Cooldown, waits[0].kind);
    TEST_ASSERT_EQUAL(250, waits[0].remainingSeconds);
}

// SC-003: random roof conditions over time. Each condition event is never
// sent twice within the cooldown, never in the grace, never while switched
// off; and a condition that held for its settle time plus the cooldown
// (outside the grace, switched on) has been announced by then.
void test_cooldown_zero_still_spaces_roof_alerts_a_minute(void)
{
    AlertEngine engine = started();
    const AlertRules noCooldown = rules(GRACE, 0);
    Roof closing;
    closing.closing = true;
    std::vector<Alert> all;
    // The verdict flips every 2 s for 3 minutes.
    for (uint32_t t = 100; t < 280; ++t)
    {
        std::vector<Alert> out = engine.update(at(t, ((t / 2) % 2) == 0 ? closing : Roof{}), noCooldown);
        all.insert(all.end(), out.begin(), out.end());
    }
    TEST_ASSERT_EQUAL(3, count(all, AlertType::UnsafeClosing)); // 100, 160, 220
}

void test_property_random_roof_conditions(void)
{
    std::mt19937 rng(7007);
    struct Check
    {
        AlertType type;
        uint32_t settle;
    };
    const Check checks[] = {
        {AlertType::MoveFault, 0},
        {AlertType::SupplyLow, ROOF_SETTLE_SECONDS},
        {AlertType::SafetyLost, ROOF_SETTLE_SECONDS},
        {AlertType::UnsafeClosing, 0},
    };
    for (int runIndex = 0; runIndex < 5000; ++runIndex)
    {
        AlertEngine engine;
        const uint32_t setting = std::uniform_int_distribution<uint32_t>(0, 400)(rng);
        const uint32_t cooldown = roofCooldown(setting);
        AlertRules r = rules(GRACE, setting);
        r.onSupplyLow = (rng() % 5) != 0;
        bool state[4] = {};
        uint32_t heldSince[4] = {};
        bool announcedThisSpell[4] = {};
        bool everSent[4] = {};
        uint32_t lastSent[4] = {};
        const uint32_t flipOdds = 5 + rng() % 120;
        for (uint32_t t = 0; t < 1500; ++t)
        {
            for (int i = 0; i < 4; ++i)
                if (rng() % flipOdds == 0)
                {
                    state[i] = !state[i];
                    heldSince[i] = t;
                    announcedThisSpell[i] = false;
                }
            Roof roof{state[0], state[1], state[2], state[3], 0};
            const std::vector<Alert> out = engine.update(at(t, roof), r);
            for (int i = 0; i < 4; ++i)
            {
                const size_t n = count(out, checks[i].type);
                const bool on = i != 1 || r.onSupplyLow;
                TEST_ASSERT_TRUE(n <= 1);
                if (n == 1)
                {
                    TEST_ASSERT_TRUE_MESSAGE(t >= GRACE, "sent in the grace");
                    TEST_ASSERT_TRUE_MESSAGE(on, "sent while switched off");
                    TEST_ASSERT_TRUE_MESSAGE(state[i], "sent while the condition is absent");
                    TEST_ASSERT_TRUE_MESSAGE(t - heldSince[i] >= checks[i].settle, "sent before its settle time");
                    TEST_ASSERT_TRUE_MESSAGE(!everSent[i] || t - lastSent[i] >= cooldown, "sent twice within the cooldown");
                    TEST_ASSERT_FALSE_MESSAGE(announcedThisSpell[i], "sent twice in one spell");
                    everSent[i] = true;
                    lastSent[i] = t;
                    announcedThisSpell[i] = true;
                }
                // Liveness: switched on for the whole spell, which started after
                // the grace (a spell already present while switched off counts
                // as announced).
                const uint32_t due = (heldSince[i] > GRACE ? heldSince[i] : GRACE) + checks[i].settle + cooldown;
                if (on && state[i] && heldSince[i] > 0 && t >= due)
                    TEST_ASSERT_TRUE_MESSAGE(announcedThisSpell[i], "a lasting condition was never announced");
            }
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_names_and_critical);
    RUN_TEST(test_fault_alerts_once_with_its_text);
    RUN_TEST(test_fault_again_after_a_clear_waits_out_the_cooldown);
    RUN_TEST(test_a_held_fault_that_clears_is_not_sent);
    RUN_TEST(test_supply_settles_for_30_seconds);
    RUN_TEST(test_supply_dip_shorter_than_the_settle_is_ignored);
    RUN_TEST(test_source_settles_and_names_the_source);
    RUN_TEST(test_nothing_in_the_grace_then_a_present_condition_is_announced);
    RUN_TEST(test_an_event_switched_off_follows_silently);
    RUN_TEST(test_closing_once_per_cooldown_with_a_flapping_verdict);
    RUN_TEST(test_closed_once_per_close_not_held_by_the_cooldown);
    RUN_TEST(test_closed_in_the_grace_is_sent_when_it_ends);
    RUN_TEST(test_closed_switched_off_is_dropped);
    RUN_TEST(test_unknown_roof_is_ignored);
    RUN_TEST(test_announced_bits_survive_a_reset);
    RUN_TEST(test_a_restored_bit_for_a_cleared_condition_is_dropped);
    RUN_TEST(test_stacked_with_other_roof_events);
    RUN_TEST(test_waits_report_roof_settle_and_cooldown);
    RUN_TEST(test_cooldown_zero_still_spaces_roof_alerts_a_minute);
    RUN_TEST(test_property_random_roof_conditions);
    return UNITY_END();
}
