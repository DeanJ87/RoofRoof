// Spec 053 (findings F55/F56): alerts can be on without touching roof control.
// The main loop hands HTTPS deliveries to the alert task through a fixed queue
// that never waits or allocates; when full, the oldest non-critical delivery
// makes room and a critical one keeps its place; the alert task takes critical
// ones first. A TLS session starts only with the roof still and the heap above
// the TLS floor, waiting with a back-off otherwise and giving up (to be sent
// again by the outbox) after 10 minutes. The simulation runs a move with alerts
// raised all through it: no session starts while the roof moves, every hand-over
// returns at once, no critical alert is lost.

#include <unity.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#include "AlertEngine.h"
#include "AlertSend.h"
#include "HeapGuard.h"

using namespace SQM;
using namespace SQM::Alerts;

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
    using Queue = SendQueue<int, 4>;

    std::vector<int> drain(Queue &queue)
    {
        std::vector<int> out;
        int item = 0;
        while (queue.pop(item))
            out.push_back(item);
        return out;
    }
} // namespace

// ---- the queue ----

void test_oldest_first_when_none_is_critical(void)
{
    Queue queue;
    for (int i = 1; i <= 3; ++i)
        TEST_ASSERT_TRUE(queue.push(i, false).accepted);
    TEST_ASSERT_EQUAL(3, queue.size());
    const std::vector<int> out = drain(queue);
    TEST_ASSERT_EQUAL(3, out.size());
    TEST_ASSERT_EQUAL(1, out[0]);
    TEST_ASSERT_EQUAL(2, out[1]);
    TEST_ASSERT_EQUAL(3, out[2]);
    TEST_ASSERT_TRUE(queue.empty());
}

void test_critical_ones_are_taken_first(void)
{
    Queue queue;
    queue.push(1, false);
    queue.push(2, true);
    queue.push(3, false);
    queue.push(4, true);
    const std::vector<int> out = drain(queue);
    TEST_ASSERT_EQUAL(2, out[0]);
    TEST_ASSERT_EQUAL(4, out[1]);
    TEST_ASSERT_EQUAL(1, out[2]);
    TEST_ASSERT_EQUAL(3, out[3]);
}

void test_full_pushes_out_the_oldest_non_critical(void)
{
    Queue queue;
    queue.push(1, true);
    queue.push(2, false);
    queue.push(3, false);
    queue.push(4, true);
    const Queue::Push pushed = queue.push(5, false);
    TEST_ASSERT_TRUE(pushed.accepted);
    TEST_ASSERT_TRUE(pushed.dropped);
    TEST_ASSERT_EQUAL(2, pushed.victim);
    TEST_ASSERT_EQUAL(4, queue.size());
    TEST_ASSERT_EQUAL(1u, queue.dropped());
    const std::vector<int> out = drain(queue);
    TEST_ASSERT_EQUAL(1, out[0]);
    TEST_ASSERT_EQUAL(4, out[1]);
    TEST_ASSERT_EQUAL(3, out[2]);
    TEST_ASSERT_EQUAL(5, out[3]);
}

void test_a_critical_one_pushes_out_a_non_critical_one_first(void)
{
    Queue queue;
    queue.push(1, true);
    queue.push(2, true);
    queue.push(3, false);
    queue.push(4, true);
    const Queue::Push pushed = queue.push(5, true);
    TEST_ASSERT_TRUE(pushed.accepted);
    TEST_ASSERT_EQUAL(3, pushed.victim);
    const std::vector<int> out = drain(queue);
    TEST_ASSERT_EQUAL(1, out[0]);
    TEST_ASSERT_EQUAL(2, out[1]);
    TEST_ASSERT_EQUAL(4, out[2]);
    TEST_ASSERT_EQUAL(5, out[3]);
}

void test_all_critical_refuses_a_non_critical_one(void)
{
    Queue queue;
    for (int i = 1; i <= 4; ++i)
        queue.push(i, true);
    const Queue::Push pushed = queue.push(9, false);
    TEST_ASSERT_FALSE(pushed.accepted);
    TEST_ASSERT_TRUE(pushed.dropped);
    TEST_ASSERT_EQUAL(9, pushed.victim);
    TEST_ASSERT_EQUAL(1u, queue.dropped());
    const std::vector<int> out = drain(queue);
    TEST_ASSERT_EQUAL(4, out.size());
    TEST_ASSERT_EQUAL(1, out[0]);
}

void test_all_critical_a_newer_critical_one_replaces_the_oldest(void)
{
    // The outbox (spec 030) sends the pushed-out critical alert again.
    Queue queue;
    for (int i = 1; i <= 4; ++i)
        queue.push(i, true);
    const Queue::Push pushed = queue.push(5, true);
    TEST_ASSERT_TRUE(pushed.accepted);
    TEST_ASSERT_EQUAL(1, pushed.victim);
    const std::vector<int> out = drain(queue);
    TEST_ASSERT_EQUAL(2, out[0]);
    TEST_ASSERT_EQUAL(5, out[3]);
}

void test_the_queue_never_allocates(void)
{
    Queue queue;
    const size_t before = allocations.load();
    int item = 0;
    for (int i = 0; i < 1000; ++i)
    {
        queue.push(i, (i % 3) == 0);
        if (i % 2)
            queue.pop(item);
    }
    TEST_ASSERT_EQUAL(before, allocations.load());
    TEST_ASSERT_TRUE(queue.size() <= Queue::CAPACITY);
}

void test_order_survives_many_pushes(void)
{
    // The order counter keeps the oldest first across a long run.
    Queue queue;
    int item = 0;
    for (int i = 0; i < 100000; ++i)
    {
        queue.push(i, false);
        queue.pop(item);
        TEST_ASSERT_EQUAL(i, item);
    }
    queue.push(1, false);
    queue.push(2, false);
    queue.pop(item);
    TEST_ASSERT_EQUAL(1, item);
}

// ---- the TLS floor ----

void test_tls_floor_boundaries(void)
{
    using namespace HeapSafe;
    TEST_ASSERT_EQUAL_UINT32(45056u, TLS_MIN_LARGEST_BLOCK_BYTES);
    TEST_ASSERT_EQUAL_UINT32(57344u, TLS_MIN_FREE_BYTES);
    TEST_ASSERT_TRUE(tlsFits(TLS_MIN_LARGEST_BLOCK_BYTES, TLS_MIN_FREE_BYTES));
    TEST_ASSERT_FALSE(tlsFits(TLS_MIN_LARGEST_BLOCK_BYTES - 1, TLS_MIN_FREE_BYTES));
    TEST_ASSERT_FALSE(tlsFits(TLS_MIN_LARGEST_BLOCK_BYTES, TLS_MIN_FREE_BYTES - 1));
    // F55: the device when the alerts went out (largest block 5,364 B).
    TEST_ASSERT_FALSE(tlsFits(5364, 30000));
    // F51: the board at rest on spec 045 (largest 38.9 KB, free 63.7 KB): waits.
    TEST_ASSERT_FALSE(tlsFits(38900, 63680));
    // Spec 038 at rest (largest 57 KB, free 68 KB): goes.
    TEST_ASSERT_TRUE(tlsFits(57300, 67600));
    // The TLS floor is above the alert path's own (spec 035).
    TEST_ASSERT_TRUE(alertFits(TLS_MIN_LARGEST_BLOCK_BYTES, TLS_MIN_FREE_BYTES));
}

// ---- the gate ----

void test_gate_goes_when_still_and_with_room(void)
{
    SendGate gate;
    gate.start(1000);
    TEST_ASSERT_EQUAL(SendWait::Go, gate.check(1000, false, true));
    TEST_ASSERT_EQUAL_UINT32(0, gate.retryInMs());
    TEST_ASSERT_EQUAL_UINT32(0, gate.memoryWaits() + gate.moveWaits() + gate.gaveUp());
}

void test_gate_waits_while_the_roof_moves(void)
{
    SendGate gate;
    gate.start(0);
    // A move comes first, whatever the heap.
    TEST_ASSERT_EQUAL(SendWait::RoofMoving, gate.check(0, true, true));
    TEST_ASSERT_EQUAL_UINT32(SEND_MOVE_RECHECK_MS, gate.retryInMs());
    TEST_ASSERT_EQUAL(SendWait::RoofMoving, gate.check(1000, true, false));
    TEST_ASSERT_EQUAL_UINT32(2, gate.moveWaits());
    TEST_ASSERT_EQUAL(SendWait::Go, gate.check(2000, false, true));
}

void test_gate_backs_off_while_memory_is_short(void)
{
    SendGate gate;
    gate.start(0);
    const uint32_t expected[] = {2000, 4000, 8000, 16000, 30000, 30000};
    uint32_t now = 0;
    for (uint32_t wait : expected)
    {
        TEST_ASSERT_EQUAL(SendWait::ShortOfMemory, gate.check(now, false, false));
        TEST_ASSERT_EQUAL_UINT32(wait, gate.retryInMs());
        now += wait;
    }
    TEST_ASSERT_EQUAL_UINT32(6, gate.memoryWaits());
    TEST_ASSERT_EQUAL(SendWait::Go, gate.check(now, false, true));
    // A new delivery starts its back-off again.
    gate.start(now);
    TEST_ASSERT_EQUAL(SendWait::ShortOfMemory, gate.check(now, false, false));
    TEST_ASSERT_EQUAL_UINT32(SEND_MEMORY_FIRST_MS, gate.retryInMs());
}

void test_gate_gives_up_after_ten_minutes(void)
{
    SendGate gate;
    gate.start(5000);
    TEST_ASSERT_EQUAL(SendWait::ShortOfMemory, gate.check(5000 + SEND_GIVE_UP_MS - 1, false, false));
    TEST_ASSERT_EQUAL(SendWait::GiveUp, gate.check(5000 + SEND_GIVE_UP_MS, false, false));
    TEST_ASSERT_EQUAL_UINT32(1, gate.gaveUp());
    TEST_ASSERT_EQUAL_STRING("Waited 10 min: sent again later", sendWaitText(SendWait::GiveUp));
    // Room at the last moment still goes.
    gate.start(0);
    TEST_ASSERT_EQUAL(SendWait::Go, gate.check(SEND_GIVE_UP_MS + 1, false, true));
}

void test_gate_texts_fit_a_record(void)
{
    // AlertRecord::DETAIL_BYTES is 32 with the terminator.
    for (SendWait wait : {SendWait::RoofMoving, SendWait::ShortOfMemory, SendWait::GiveUp})
        TEST_ASSERT_TRUE(std::char_traits<char>::length(sendWaitText(wait)) < 32);
}

// ---- a move with alerts raised all through it ----

namespace
{
    struct Delivery
    {
        int id;
        bool critical;
    };

    struct Session
    {
        uint32_t startMs;
        int id;
        bool critical;
    };

    // The alert task on the simulated clock: one delivery at a time, asking
    // the gate at its retry times; a session takes `sessionMs`.
    struct AlertTask
    {
        SendQueue<Delivery, 8> *queue;
        SendGate gate;
        bool busy = false;
        Delivery current{};
        uint32_t nextCheckMs = 0;
        uint32_t sessionEndsMs = 0;
        bool inSession = false;
        std::vector<Session> sessions;

        void step(uint32_t now, bool moving, bool fits, uint32_t sessionMs)
        {
            if (inSession)
            {
                if (now < sessionEndsMs)
                    return;
                inSession = false;
                busy = false;
            }
            if (!busy)
            {
                if (!queue->pop(current))
                    return;
                busy = true;
                gate.start(now);
                nextCheckMs = now;
            }
            if (now < nextCheckMs)
                return;
            const SendWait wait = gate.check(now, moving, fits);
            if (wait == SendWait::Go)
            {
                sessions.push_back({now, current.id, current.critical});
                inSession = true;
                sessionEndsMs = now + sessionMs;
                return;
            }
            if (wait == SendWait::GiveUp)
            {
                busy = false;
                return;
            }
            nextCheckMs = now + gate.retryInMs();
        }
    };
} // namespace

namespace
{
    constexpr uint32_t STEP_MS = 20;         // the roof's control period
    constexpr uint32_t MOVE_FROM_MS = 10000; // the roof moves from 10 s ...
    constexpr uint32_t MOVE_TO_MS = 70000;   // ... to 70 s
    constexpr uint32_t SESSION_MS = 4000;    // a slow TLS handshake and request

    // The main loop's side: raises an alert every 3 s until 90 s (a busy
    // night: the outbox sends at most one a second), every fifth critical.
    struct MainLoop
    {
        SendQueue<Delivery, 8> *queue;
        int nextId = 1;
        std::vector<int> criticalIds;
        size_t droppedCritical = 0;
        size_t deepest = 0;

        void step(uint32_t now)
        {
            if (now < 90000 && now % 3000 == 0)
                raise();
            if (queue->size() > deepest)
                deepest = queue->size();
        }

        // A hand-over is one call that returns at once and allocates nothing.
        void raise()
        {
            const bool critical = nextId % 5 == 0;
            const size_t before = allocations.load();
            const auto pushed = queue->push({nextId, critical}, critical);
            TEST_ASSERT_EQUAL(before, allocations.load());
            if (critical)
                criticalIds.push_back(nextId);
            if (pushed.dropped && pushed.victim.critical)
                ++droppedCritical;
            ++nextId;
        }
    };

    bool sentDuringMove(const std::vector<Session> &sessions)
    {
        for (const Session &s : sessions)
            if (s.startMs >= MOVE_FROM_MS && s.startMs < MOVE_TO_MS)
                return true;
        return false;
    }

    bool wasSent(const std::vector<Session> &sessions, int id)
    {
        for (const Session &s : sessions)
            if (s.id == id)
                return true;
        return false;
    }
} // namespace

void test_a_move_with_alerts_raised_throughout(void)
{
    SendQueue<Delivery, 8> queue;
    AlertTask task;
    task.queue = &queue;
    MainLoop loop;
    loop.queue = &queue;

    for (uint32_t now = 0; now <= 180000; now += STEP_MS)
    {
        const bool moving = now >= MOVE_FROM_MS && now < MOVE_TO_MS;
        // The heap dips during the move's first seconds (finding F52).
        const bool fits = !(now >= MOVE_FROM_MS && now < MOVE_FROM_MS + 6000);
        loop.step(now);
        task.step(now, moving, fits, SESSION_MS);
    }

    TEST_ASSERT_TRUE(loop.deepest <= 8);
    TEST_ASSERT_TRUE(queue.dropped() > 0); // the burst outran the sends
    TEST_ASSERT_TRUE(task.gate.moveWaits() > 0);
    // No TLS session starts while the roof moves.
    TEST_ASSERT_FALSE(sentDuringMove(task.sessions));
    // Every critical alert was sent: none pushed out (fewer than 8 waited at once).
    TEST_ASSERT_EQUAL(0, loop.droppedCritical);
    for (int id : loop.criticalIds)
        TEST_ASSERT_TRUE_MESSAGE(wasSent(task.sessions, id), "a critical alert was never sent");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_oldest_first_when_none_is_critical);
    RUN_TEST(test_critical_ones_are_taken_first);
    RUN_TEST(test_full_pushes_out_the_oldest_non_critical);
    RUN_TEST(test_a_critical_one_pushes_out_a_non_critical_one_first);
    RUN_TEST(test_all_critical_refuses_a_non_critical_one);
    RUN_TEST(test_all_critical_a_newer_critical_one_replaces_the_oldest);
    RUN_TEST(test_the_queue_never_allocates);
    RUN_TEST(test_order_survives_many_pushes);
    RUN_TEST(test_tls_floor_boundaries);
    RUN_TEST(test_gate_goes_when_still_and_with_room);
    RUN_TEST(test_gate_waits_while_the_roof_moves);
    RUN_TEST(test_gate_backs_off_while_memory_is_short);
    RUN_TEST(test_gate_gives_up_after_ten_minutes);
    RUN_TEST(test_gate_texts_fit_a_record);
    RUN_TEST(test_a_move_with_alerts_raised_throughout);
    return UNITY_END();
}
