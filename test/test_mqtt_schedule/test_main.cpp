// The roof over MQTT (spec 008): when the state document is due (research R4)
// and how much of a main-loop pass MQTT may take.

#include <string>
#include <unity.h>
#include <vector>

#include "Config.h" // RoofIoLogic builds lib/ConfigModel (SettingsGuard); name it so the native build finds its dependencies
#include "RoofMqtt.h"
#include "RoofStatus.h"

using namespace SQM;
using namespace SQM::RoofIo;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const std::string TOPIC = "roofroof/roof/set";

    Snapshot closedRoof()
    {
        Snapshot s;
        s.status.state = Roof::State::Closed;
        s.status.positionKnown = true;
        s.status.positionMm = 0.0f;
        s.status.hall = Roof::HallLevel::Low;
        s.status.hallCounts = 700;
        s.supplyVolts = 12.44f;
        s.weather = Safety::Verdict::Safe;
        s.weatherText = "safe";
        s.parkConfirmed = true;
        s.parkText = "parked";
        return s;
    }
} // namespace

void test_first_publish_and_each_connection(void)
{
    Mqtt::StateSchedule schedule;
    const Mqtt::StateKey key = Mqtt::keyOf(closedRoof());
    TEST_ASSERT_TRUE(schedule.due(key, 1, 0, 60000));
    schedule.published(key, 1, 0, true);
    TEST_ASSERT_FALSE(schedule.due(key, 1, 10, 60000));
    TEST_ASSERT_TRUE(schedule.due(key, 2, 10, 60000)); // reconnected: at once
}

void test_a_change_is_due_after_the_minimum_gap(void)
{
    Mqtt::StateSchedule schedule;
    Snapshot s = closedRoof();
    schedule.published(Mqtt::keyOf(s), 1, 1000, true);
    // Every field of the key makes a change (the roof at rest).
    Snapshot changed[6] = {s, s, s, s, s, s};
    changed[0].status.state = Roof::State::Open;
    changed[1].status.fault.kind = Roof::FaultKind::Timeout;
    changed[2].status.positionKnown = false;
    changed[3].closePhase = "heldByPerson";
    changed[4].weather = Safety::Verdict::Unsafe;
    changed[5].parkConfirmed = false;
    for (const Snapshot &c : changed)
    {
        const Mqtt::StateKey key = Mqtt::keyOf(c);
        TEST_ASSERT_TRUE(key != Mqtt::keyOf(s));
        TEST_ASSERT_FALSE(schedule.due(key, 1, 1000 + Mqtt::CHANGE_MIN_MS - 1, 60000));
        TEST_ASSERT_TRUE(schedule.due(key, 1, 1000 + Mqtt::CHANGE_MIN_MS, 60000));
    }
    // Readings alone (Hall counts, the supply even across its minimum: an
    // unfiltered reading, position) are not a change.
    Snapshot readings = s;
    readings.status.hallCounts = 701;
    readings.supplyVolts = 10.0f;
    readings.status.positionMm = 3.0f;
    readings.weatherText = "safe (SQMeter MQTT)";
    TEST_ASSERT_TRUE(Mqtt::keyOf(readings) == Mqtt::keyOf(s));
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(readings), 1, 5000, 60000));
}

void test_the_interval_at_rest(void)
{
    Mqtt::StateSchedule schedule;
    const Mqtt::StateKey key = Mqtt::keyOf(closedRoof());
    schedule.published(key, 1, 0, true);
    TEST_ASSERT_FALSE(schedule.due(key, 1, 59999, 60000));
    TEST_ASSERT_TRUE(schedule.due(key, 1, 60000, 60000));
    // Across the millis() wrap.
    schedule.published(key, 1, 4294967000u, true);
    TEST_ASSERT_FALSE(schedule.due(key, 1, 100, 60000));
    TEST_ASSERT_TRUE(schedule.due(key, 1, 59704, 60000));
}

// Research R4: while the roof moves or a close waits, nothing periodic.
void test_nothing_periodic_while_moving(void)
{
    for (Roof::State moving : {Roof::State::Opening, Roof::State::Closing})
    {
        Snapshot s = closedRoof();
        s.status.state = moving;
        Mqtt::StateSchedule schedule;
        const Mqtt::StateKey key = Mqtt::keyOf(s);
        schedule.published(key, 1, 0, true);
        TEST_ASSERT_FALSE(schedule.due(key, 1, 600000, 1000));
        // The end of the move goes out within CHANGE_MIN_MS.
        Snapshot stopped = s;
        stopped.status.state = Roof::State::Open;
        TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(stopped), 1, Mqtt::CHANGE_MIN_MS - 1, 1000));
        TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(stopped), 1, Mqtt::CHANGE_MIN_MS, 1000));
        // While still moving, the weather, park or supply changing is not enough.
        Snapshot flapping = s;
        flapping.weather = Safety::Verdict::Unsafe;
        flapping.parkConfirmed = false;
        flapping.status.positionKnown = false;
        TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(flapping), 1, 600000, 1000));
        // Nor is a phase that isn't an attempt starting or ending.
        Snapshot phase = s;
        phase.closePhase = "heldByPerson";
        TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(phase), 1, 600000, 1000));
        // A reversal while moving waits BUSY_MIN_MS.
        Snapshot reversed = s;
        reversed.status.state = moving == Roof::State::Opening ? Roof::State::Closing : Roof::State::Opening;
        TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(reversed), 1, Mqtt::BUSY_MIN_MS - 1, 1000));
        TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(reversed), 1, Mqtt::BUSY_MIN_MS, 1000));
        // So does a reconnect.
        TEST_ASSERT_TRUE(schedule.due(key, 2, 600000, 1000));
    }
    Snapshot waiting = closedRoof();
    waiting.status.state = Roof::State::Open;
    waiting.closePending = true;
    Mqtt::StateSchedule schedule;
    schedule.published(Mqtt::keyOf(waiting), 1, 0, true);
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(waiting), 1, 600000, 1000));
}

// A close in unsafe weather with every input flapping every 100 ms (the
// weather, the park check, the supply around its minimum, the phase, even the
// state between opening and closing): at most BUSY_MAX_PUBLISHES publishes
// until the roof is at rest, so a half-open broker link can't keep the main
// loop blocked through the close.
void test_a_flapping_close_publishes_a_bounded_number_of_times(void)
{
    Mqtt::StateSchedule schedule;
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Open;
    schedule.published(Mqtt::keyOf(s), 1, 0, true);
    s.status.state = Roof::State::Closing;
    int publishes = 0;
    const uint32_t closeMs = 60000;
    for (uint32_t t = 100; t <= closeMs; t += 100)
    {
        const bool odd = (t / 100) % 2 == 1;
        s.weather = odd ? Safety::Verdict::Unsafe : Safety::Verdict::Unknown;
        s.parkConfirmed = odd;
        s.supplyVolts = odd ? 10.0f : 12.0f;
        s.closePhase = odd ? "closing" : "waitPark";
        s.status.state = odd ? Roof::State::Closing : Roof::State::Opening;
        const Mqtt::StateKey key = Mqtt::keyOf(s);
        if (schedule.due(key, 1, t, 1000))
        {
            ++publishes;
            schedule.published(key, 1, t, true);
        }
    }
    TEST_ASSERT_TRUE(publishes <= Mqtt::BUSY_MAX_PUBLISHES);
    // At rest again: published, and the count starts afresh.
    s.status.state = Roof::State::Closed;
    s.closePhase = "idle";
    TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(s), 1, closeMs + Mqtt::CHANGE_MIN_MS, 1000));
}

// After a failed publish on this connection, nothing more while busy.
void test_nothing_more_while_busy_after_a_failed_publish(void)
{
    Mqtt::StateSchedule schedule;
    Snapshot s = closedRoof();
    s.status.state = Roof::State::Closing;
    schedule.published(Mqtt::keyOf(s), 1, 0, false);
    Snapshot fault = s;
    fault.status.fault.kind = Roof::FaultKind::Timeout;
    fault.closePhase = "halted";
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(fault), 1, 600000, 1000));
    // At rest it is tried again, and a new connection starts afresh.
    Snapshot rest = s;
    rest.status.state = Roof::State::Closed;
    TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(rest), 1, 600000, 1000));
    TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(fault), 2, 600000, 1000));
    schedule.published(Mqtt::keyOf(s), 2, 600000, true);
    TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(fault), 2, 600000 + Mqtt::BUSY_MIN_MS, 1000));
}

// The automatic close's attempt counts as busy; a sequence waiting for a person doesn't.
void test_an_attempting_close_is_busy(void)
{
    for (const char *phase : {"waitPark", "closing", "retryWait"})
        TEST_ASSERT_TRUE_MESSAGE(Mqtt::closeAttempting(phase), phase);
    for (const char *phase : {"idle", "heldByPerson", "cannotMove", "halted", ""})
        TEST_ASSERT_FALSE_MESSAGE(Mqtt::closeAttempting(phase), phase);
    // Waiting for the park with the roof open: nothing periodic, the weather
    // and park flapping isn't a change, and the attempt starting is.
    Snapshot open = closedRoof();
    open.status.state = Roof::State::Open;
    Mqtt::StateSchedule schedule;
    schedule.published(Mqtt::keyOf(open), 1, 0, true);
    Snapshot waiting = open;
    waiting.closePhase = "waitPark";
    waiting.weather = Safety::Verdict::Unsafe;
    TEST_ASSERT_TRUE(Mqtt::busy(Mqtt::keyOf(waiting)));
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(waiting), 1, Mqtt::BUSY_MIN_MS - 1, 1000));
    TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(waiting), 1, Mqtt::BUSY_MIN_MS, 1000));
    schedule.published(Mqtt::keyOf(waiting), 1, Mqtt::BUSY_MIN_MS, true);
    Snapshot flapping = waiting;
    flapping.weather = Safety::Verdict::Unknown;
    flapping.parkConfirmed = false;
    TEST_ASSERT_FALSE(schedule.due(Mqtt::keyOf(flapping), 1, 600000, 1000));
    // Held by a person: at rest again, so the interval applies.
    Snapshot held = waiting;
    held.closePhase = "heldByPerson";
    TEST_ASSERT_FALSE(Mqtt::busy(Mqtt::keyOf(held)));
    TEST_ASSERT_TRUE(schedule.due(Mqtt::keyOf(held), 1, 600000, 1000));
}

// The coordinator's rule for a busy roof: one main-loop pass of MQTT takes at
// most BUSY_PASS_BUDGET_MS, a single write at most BUSY_WRITE_BUDGET_MS, and
// a write with nothing left gets 0 (drop the connection, never wait).
void test_the_pass_budget_while_busy(void)
{
    Mqtt::PassBudget budget;
    budget.start(1000);
    TEST_ASSERT_EQUAL_UINT32(Mqtt::BUSY_WRITE_BUDGET_MS, budget.writeBudgetMs(1000, true));
    TEST_ASSERT_EQUAL_UINT32(Mqtt::BUSY_WRITE_BUDGET_MS, budget.writeBudgetMs(1250, true));
    TEST_ASSERT_EQUAL_UINT32(100, budget.writeBudgetMs(1400, true)); // only what is left
    TEST_ASSERT_EQUAL_UINT32(1, budget.writeBudgetMs(1499, true));
    TEST_ASSERT_EQUAL_UINT32(0, budget.writeBudgetMs(1500, true));
    TEST_ASSERT_EQUAL_UINT32(0, budget.writeBudgetMs(9000, true));
    TEST_ASSERT_TRUE(budget.mayRead(1499, true));
    TEST_ASSERT_FALSE(budget.mayRead(1500, true));
    // Writes taking every millisecond offered never add up past the pass budget.
    uint32_t now = 1000;
    for (int i = 0; i < 20; ++i)
        now += budget.writeBudgetMs(now, true);
    TEST_ASSERT_EQUAL_UINT32(1000 + Mqtt::BUSY_PASS_BUDGET_MS, now);
    TEST_ASSERT_TRUE(Mqtt::BUSY_PASS_BUDGET_MS <= 500);
    // A new pass starts afresh, across the millis() wrap too.
    budget.start(4294967200u);
    TEST_ASSERT_EQUAL_UINT32(Mqtt::BUSY_WRITE_BUDGET_MS, budget.writeBudgetMs(100, true));
    TEST_ASSERT_EQUAL_UINT32(0, budget.writeBudgetMs(404, true));
}

void test_the_pass_budget_at_rest(void)
{
    Mqtt::PassBudget budget;
    budget.start(0);
    TEST_ASSERT_EQUAL_UINT32(Mqtt::REST_WRITE_BUDGET_MS, budget.writeBudgetMs(60000, false));
    TEST_ASSERT_TRUE(budget.mayRead(60000, false));
}

// A read while busy only runs when a whole packet is buffered, so PubSubClient
// never waits (up to its socket timeout) for the rest of one.
void test_packet_complete(void)
{
    std::vector<uint8_t> packet{0x30, static_cast<uint8_t>(2 + TOPIC.size() + 4), 0x00, static_cast<uint8_t>(TOPIC.size())};
    packet.insert(packet.end(), TOPIC.begin(), TOPIC.end());
    packet.insert(packet.end(), {'O', 'P', 'E', 'N'});
    TEST_ASSERT_TRUE(Mqtt::packetComplete(packet.data(), packet.size()));
    TEST_ASSERT_FALSE(Mqtt::packetComplete(packet.data(), packet.size() - 1));
    TEST_ASSERT_FALSE(Mqtt::packetComplete(packet.data(), 1));
    TEST_ASSERT_FALSE(Mqtt::packetComplete(nullptr, 0));
    // A whole packet with more behind it.
    std::vector<uint8_t> two = packet;
    two.push_back(0xD0);
    TEST_ASSERT_TRUE(Mqtt::packetComplete(two.data(), two.size()));
    // Two-byte remaining length (200 bytes): incomplete until all 203 bytes are here.
    std::vector<uint8_t> big{0x30, 0xC8, 0x01};
    big.resize(3 + 200, 'x');
    TEST_ASSERT_TRUE(Mqtt::packetComplete(big.data(), big.size()));
    TEST_ASSERT_FALSE(Mqtt::packetComplete(big.data(), big.size() - 1));
    TEST_ASSERT_FALSE(Mqtt::packetComplete(big.data(), 2)); // the length field cut short
    // PINGRESP: no body.
    const uint8_t pingresp[] = {0xD0, 0x00};
    TEST_ASSERT_TRUE(Mqtt::packetComplete(pingresp, 2));
    // Five continuation bytes: not MQTT.
    const uint8_t bad[] = {0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0, 0};
    TEST_ASSERT_FALSE(Mqtt::packetComplete(bad, sizeof bad));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_publish_and_each_connection);
    RUN_TEST(test_a_change_is_due_after_the_minimum_gap);
    RUN_TEST(test_the_interval_at_rest);
    RUN_TEST(test_nothing_periodic_while_moving);
    RUN_TEST(test_a_flapping_close_publishes_a_bounded_number_of_times);
    RUN_TEST(test_nothing_more_while_busy_after_a_failed_publish);
    RUN_TEST(test_an_attempting_close_is_busy);
    RUN_TEST(test_the_pass_budget_while_busy);
    RUN_TEST(test_the_pass_budget_at_rest);
    RUN_TEST(test_packet_complete);
    return UNITY_END();
}
