// Spec 035: the alert path is heap-safe. A burst of queued alerts goes out
// one per step, the queue's steps allocate nothing and a push never grows the
// queue; the alert channels' availability is worked out for every send
// without allocating (it built every setting's entry list, and a burst of
// alerts aborted the device there); the settings list is one allocation.

#include <unity.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "AlertQueue.h"
#include "Config.h"
#include "HeapGuard.h"
#include "SettingsDeps.h"

using namespace SQM;
using namespace SQM::CloseSeq;

// Every allocation in this test binary is counted.
static std::atomic<size_t> allocations{0};
void *operator new(std::size_t size)
{
    ++allocations;
    void *p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}

void setUp(void) {}
void tearDown(void) {}

namespace
{
    const char *const CHANNEL_SETTINGS[] = {
        "alerts.pushover.enabled", "alerts.ntfy.enabled", "alerts.webhook.enabled", "alerts.mqtt.enabled"};

    Config channelsOn()
    {
        Config cfg = Config::createDefault();
        cfg.alerts.pushoverEnabled = cfg.alerts.ntfyEnabled = cfg.alerts.webhookEnabled = cfg.alerts.mqttEnabled = true;
        return cfg;
    }

    Alerts::Alert alertOf(Alerts::AlertType type)
    {
        Alerts::Alert alert;
        alert.type = type;
        alert.title = "Title";
        alert.message = "A message long enough to live on the heap, as real ones do";
        alert.vars = {{"reason", "why"}};
        return alert;
    }

    // The burst from the crash: weather unsafe then safe, an Open pre-empted by
    // the automatic close, the mount parked, N.I.N.A. lost.
    const Alerts::AlertType BURST[] = {
        Alerts::AlertType::SafetyLost,
        Alerts::AlertType::UnsafeClosing,
        Alerts::AlertType::RoofClosed,
        Alerts::AlertType::NinaLost,
        Alerts::AlertType::NeedsPerson,
        Alerts::AlertType::MoveFault,
        Alerts::AlertType::MountNotParked,
        Alerts::AlertType::DaylightUnknown,
    };
} // namespace

void test_the_channel_check_allocates_nothing(void)
{
    const Config cfg = channelsOn();
    Deps::Facts facts; // nothing connected: every channel blocked for now
    Deps::Entry entries[Deps::ALERT_CHANNEL_ENTRIES];
    const size_t before = allocations.load();
    size_t blocked = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const size_t count = Deps::alertChannels(cfg, facts, entries);
        for (const char *setting : CHANNEL_SETTINGS)
            blocked += Deps::channelReason(entries, count, setting) != nullptr ? 1 : 0;
    }
    TEST_ASSERT_EQUAL(0, allocations.load() - before);
    TEST_ASSERT_EQUAL(4000, blocked);
}

// The same answers as the full list, for every combination of the facts and
// the switches the channels depend on.
void test_the_channel_check_matches_the_full_list(void)
{
    for (unsigned bits = 0; bits < 32; ++bits)
    {
        Config cfg = channelsOn();
        cfg.alerts.enabled = (bits & 1) != 0;
        cfg.mqtt.enabled = (bits & 2) != 0;
        cfg.alerts.ntfyEnabled = (bits & 16) != 0;
        Deps::Facts facts;
        facts.wifiConnected = (bits & 4) != 0;
        facts.mqttConnected = (bits & 8) != 0;
        const std::vector<Deps::Entry> all = Deps::evaluate(cfg, facts);
        Deps::Entry entries[Deps::ALERT_CHANNEL_ENTRIES];
        const size_t count = Deps::alertChannels(cfg, facts, entries);
        TEST_ASSERT_EQUAL(Deps::ALERT_CHANNEL_ENTRIES, count);
        for (const char *setting : CHANNEL_SETTINGS)
            TEST_ASSERT_EQUAL_PTR(Deps::reasonFor(all, setting), Deps::channelReason(entries, count, setting));
    }
}

void test_the_settings_list_is_one_allocation(void)
{
    const Config cfg = channelsOn();
    const Deps::Facts facts;
    const size_t before = allocations.load();
    const std::vector<Deps::Entry> entries = Deps::evaluate(cfg, facts);
    TEST_ASSERT_EQUAL(1, allocations.load() - before);
    TEST_ASSERT_EQUAL(entries.capacity(), entries.size()); // reserved exactly: a new entry needs the count raised
}

// A push costs the alert's own copy and nothing more, however full the queue is.
void test_a_push_never_grows_the_queue(void)
{
    AlertQueue queue;
    const Alerts::Alert alert = alertOf(Alerts::AlertType::SafetyLost);
    size_t first = 0;
    for (size_t i = 0; i < sizeof(BURST) / sizeof(BURST[0]); ++i)
    {
        Alerts::Alert next = alert;
        next.type = BURST[i];
        const size_t before = allocations.load();
        queue.push(next, false);
        const size_t cost = allocations.load() - before;
        if (i == 0)
            first = cost;
        TEST_ASSERT_EQUAL(first, cost);
    }
    TEST_ASSERT_EQUAL(ALERT_QUEUE_CAPACITY, queue.size());
}

// The burst goes out one alert per step, critical first, over many steps of
// failed sends and back-off, and the steps themselves allocate nothing.
void test_a_burst_goes_out_one_per_step_without_allocating(void)
{
    AlertQueue queue;
    for (Alerts::AlertType type : BURST)
        queue.push(alertOf(type), false);

    Alerts::AlertType order[64];
    size_t sends = 0;
    bool deliver = false;
    const AlertQueue::Send send = [&](const QueuedAlert &queued) -> uint32_t
    {
        if (sends < 64)
            order[sends] = queued.alert.type;
        return static_cast<uint32_t>(++sends);
    };
    const AlertQueue::Probe probe = [&](uint32_t) { return deliver ? Delivery::Delivered : Delivery::Failed; };

    const size_t before = allocations.load();
    size_t mostInOneStep = 0;
    for (uint64_t t = 0; t < 600000; t += 1000) // ten minutes of failures
    {
        const size_t was = sends;
        queue.step(t, true, send, probe);
        mostInOneStep = sends - was > mostInOneStep ? sends - was : mostInOneStep;
    }
    deliver = true;
    for (uint64_t t = 600000; t < 620000; t += 1000)
        queue.step(t, true, send, probe);
    TEST_ASSERT_EQUAL(0, allocations.load() - before);
    TEST_ASSERT_EQUAL(1, mostInOneStep);
    TEST_ASSERT_EQUAL(0, queue.size());
    // The two critical ones that survived the full queue went first.
    TEST_ASSERT_TRUE(Alerts::isCritical(order[0]));
    TEST_ASSERT_TRUE(Alerts::isCritical(order[1]));
    TEST_ASSERT_FALSE(Alerts::isCritical(order[2]));
}

void test_an_alert_waits_while_memory_is_short(void)
{
    TEST_ASSERT_TRUE(HeapSafe::alertFits(38900, 121532));
    TEST_ASSERT_FALSE(HeapSafe::alertFits(1200, 121532)); // the crash: no 1.3 KB block left
    TEST_ASSERT_FALSE(HeapSafe::alertFits(38900, 20000));
    TEST_ASSERT_TRUE(HeapSafe::alertFits(HeapSafe::MIN_BLOCK_BYTES, HeapSafe::MIN_FREE_BYTES));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_channel_check_allocates_nothing);
    RUN_TEST(test_the_channel_check_matches_the_full_list);
    RUN_TEST(test_the_settings_list_is_one_allocation);
    RUN_TEST(test_a_push_never_grows_the_queue);
    RUN_TEST(test_a_burst_goes_out_one_per_step_without_allocating);
    RUN_TEST(test_an_alert_waits_while_memory_is_short);
    return UNITY_END();
}
