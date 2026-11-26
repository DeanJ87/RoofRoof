// Spec 050 (constitution 12.1.0, II; findings F57/F59): a park request never
// stops a slew. N.I.N.A.'s Advanced API stops a slewing mount before parking it,
// and a mount already parking reads slewing: a second request left the real
// mount stopped part-way, not parked. The sender waits for a reading that says
// the mount isn't slewing, sends nothing if it's parked, sends once otherwise,
// and sends anyway after PARK_SLEW_WAIT_MS. The roof must be at an end when it
// sends (spec 043).

#include <cstdint>
#include <unity.h>

#include "ParkRule.h"
#include "ParkSend.h"
#include "PollSchedule.h"

using namespace SQM;
using namespace SQM::Safety;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Nina::MountReading good(uint64_t at, bool slewing, bool atPark, bool connected = true)
    {
        Nina::MountReading r;
        r.status = Nina::ReadingStatus::Good;
        r.connected = connected;
        r.atPark = atPark;
        r.slewing = slewing;
        r.takenAtMs = at;
        return r;
    }

    Nina::MountReading failed(uint64_t at)
    {
        return Nina::failedReading(Nina::ReadingStatus::Unreachable, "no reply", at);
    }

    ParkSendInputs at(uint64_t now, const Nina::MountReading &reading, bool roofAtEnd = true)
    {
        ParkSendInputs in;
        in.nowMs = now;
        in.reading = reading;
        in.roofAtEnd = roofAtEnd;
        return in;
    }

    bool sends(ParkSendEvent e)
    {
        return e == ParkSendEvent::Send || e == ParkSendEvent::SendAtLimit;
    }

    // The mount: slewing until `untilMs`, then parked or merely stopped.
    struct Slew
    {
        uint64_t untilMs;
        bool parkedAfter;
    };

    // The poller: a reading every 2 s from `from` to `to`; the latest event
    // other than None in `last`. Counts the sends.
    int run(ParkSender &sender, uint64_t from, uint64_t to, Slew slew, ParkSendEvent *last = nullptr)
    {
        int sent = 0;
        for (uint64_t t = from; t <= to; t += NINA_POLL_MS)
        {
            const bool slewing = t < slew.untilMs;
            const ParkSendEvent e = sender.step(at(t, good(t, slewing, !slewing && slew.parkedAfter)));
            if (sends(e))
                ++sent;
            if (e != ParkSendEvent::None && last != nullptr)
                *last = e;
        }
        return sent;
    }
} // namespace

// F57: N.I.N.A. parks the mount itself; RoofRoof's request a few seconds later
// waits and, once the mount reads parked, sends nothing.
void test_slewing_then_parked_sends_nothing(void)
{
    ParkSender sender;
    TEST_ASSERT_TRUE(sender.request(1000));
    TEST_ASSERT_EQUAL(ParkSendEvent::Waiting, sender.step(at(1000, good(1000, true, false))));
    TEST_ASSERT_TRUE(sender.waiting());
    TEST_ASSERT_TRUE(sender.waitingForSlew());
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(3000, good(3000, true, false))));
    ParkSendEvent last = ParkSendEvent::None;
    TEST_ASSERT_EQUAL(0, run(sender, 5000, 60000, {40000, true}, &last));
    TEST_ASSERT_EQUAL(ParkSendEvent::AlreadyParked, last);
    TEST_ASSERT_FALSE(sender.waiting());
}

// A goto that ends away from park: one send once the slew stops.
void test_slewing_then_stopped_sends_once(void)
{
    ParkSender sender;
    sender.request(1000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Waiting, sender.step(at(1000, good(1000, true, false))));
    ParkSendEvent last = ParkSendEvent::None;
    TEST_ASSERT_EQUAL(1, run(sender, 3000, 90000, {30000, false}, &last));
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, last);
    TEST_ASSERT_FALSE(sender.waiting());
}

void test_already_at_park_sends_nothing(void)
{
    ParkSender sender;
    sender.request(1000);
    TEST_ASSERT_EQUAL(ParkSendEvent::AlreadyParked, sender.step(at(1000, good(1000, false, true))));
    TEST_ASSERT_FALSE(sender.waiting());
    // Not connected: sent as before (N.I.N.A. refuses it).
    sender.request(5000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, sender.step(at(5000, good(5000, false, true, false))));
}

// A mount that isn't slewing and isn't parked: sent at once, as before.
void test_still_and_not_parked_sends_at_once(void)
{
    ParkSender sender;
    sender.request(1000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, sender.step(at(1000, good(1000, false, false))));
    TEST_ASSERT_FALSE(sender.waiting());
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(3000, good(3000, false, false))));
}

// M06: a park sent into a goto fails too (the API stops the slew and parks at
// once; OnStep refuses while decelerating). At the limit the slew is stopped on
// its own (no park); the park goes once a reading after the stop says still.
void test_at_the_limit_the_slew_is_stopped_then_parked_when_still(void)
{
    ParkSender sender;
    sender.request(1000);
    ParkSendEvent last = ParkSendEvent::None;
    const uint64_t limitAt = 1000 + PARK_SLEW_WAIT_MS;
    TEST_ASSERT_EQUAL(0, run(sender, 1000, limitAt - 1, {UINT64_MAX, false}, &last));
    TEST_ASSERT_EQUAL(ParkSendEvent::Waiting, last);
    TEST_ASSERT_EQUAL(ParkSendEvent::StopSlew, sender.step(at(limitAt, good(limitAt, true, false))));
    TEST_ASSERT_TRUE(asksNina(ParkSendEvent::StopSlew));
    TEST_ASSERT_FALSE(sends(ParkSendEvent::StopSlew)); // not a park
    TEST_ASSERT_TRUE(sender.waiting());
    TEST_ASSERT_TRUE(sender.stopped());
    TEST_ASSERT_TRUE(sender.waitingForSlew());
    // A reading from the moment of the stop decides nothing; decelerating waits.
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(limitAt + 100, good(limitAt, false, false))));
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(limitAt + 2000, good(limitAt + 2000, true, false))));
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(limitAt + 4000, good(limitAt + 4000, true, false))));
    // Still: the park, once.
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, sender.step(at(limitAt + 6000, good(limitAt + 6000, false, false))));
    TEST_ASSERT_FALSE(sender.waiting());
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(limitAt + 8000, good(limitAt + 8000, false, false))));
}

// Still slewing PARK_STOP_WAIT_MS after the stop: the park anyway (warned), once.
void test_still_slewing_after_the_stop_sends_the_park_anyway(void)
{
    ParkSender sender;
    sender.request(1000);
    const uint64_t limitAt = 1000 + PARK_SLEW_WAIT_MS;
    int stops = 0;
    int parks = 0;
    ParkSendEvent last = ParkSendEvent::None;
    for (uint64_t t = 1000; t <= limitAt + PARK_STOP_WAIT_MS + 10000; t += NINA_POLL_MS)
    {
        const ParkSendEvent e = sender.step(at(t, good(t, true, false)));
        stops += e == ParkSendEvent::StopSlew ? 1 : 0;
        parks += sends(e) ? 1 : 0;
        if (e != ParkSendEvent::None)
            last = e;
        if (t < limitAt + PARK_STOP_WAIT_MS)
            TEST_ASSERT_EQUAL(0, parks);
    }
    TEST_ASSERT_EQUAL(1, stops);
    TEST_ASSERT_EQUAL(1, parks);
    TEST_ASSERT_EQUAL(ParkSendEvent::SendAtLimit, last);
    TEST_ASSERT_FALSE(sender.waiting());
}

// Parked after the stop (it was parking after all): nothing sent.
void test_parked_after_the_stop_sends_nothing(void)
{
    ParkSender sender;
    sender.request(1000);
    const uint64_t limitAt = 1000 + PARK_SLEW_WAIT_MS;
    TEST_ASSERT_EQUAL(ParkSendEvent::StopSlew, sender.step(at(limitAt, good(limitAt, true, false))));
    TEST_ASSERT_EQUAL(ParkSendEvent::AlreadyParked, sender.step(at(limitAt + 2000, good(limitAt + 2000, false, true))));
}

// The roof leaves its end after the stop: no park.
void test_roof_leaving_its_end_after_the_stop_sends_no_park(void)
{
    ParkSender sender;
    sender.request(1000);
    const uint64_t limitAt = 1000 + PARK_SLEW_WAIT_MS;
    TEST_ASSERT_EQUAL(ParkSendEvent::StopSlew, sender.step(at(limitAt, good(limitAt, true, false))));
    TEST_ASSERT_EQUAL(ParkSendEvent::Dropped, sender.step(at(limitAt + 2000, good(limitAt + 2000, false, false), false)));
    TEST_ASSERT_FALSE(sender.waiting());
}

// The close sequence and a command's park wait ask again and again while one
// waits: one wait, one send, the limit from the first request.
void test_duplicate_requests_while_waiting_send_once(void)
{
    ParkSender sender;
    TEST_ASSERT_TRUE(sender.request(1000));
    sender.step(at(1000, good(1000, true, false)));
    int sent = 0;
    int stops = 0;
    for (uint64_t t = 3000; t <= 1000 + PARK_SLEW_WAIT_MS + PARK_STOP_WAIT_MS; t += NINA_POLL_MS)
    {
        if (t % 30000 < NINA_POLL_MS)
            TEST_ASSERT_FALSE(sender.request(t));
        const ParkSendEvent e = sender.step(at(t, good(t, true, false)));
        stops += e == ParkSendEvent::StopSlew ? 1 : 0;
        sent += sends(e) ? 1 : 0;
    }
    TEST_ASSERT_EQUAL(1, stops); // at the first request's limit
    TEST_ASSERT_EQUAL(1, sent);  // the stop's own limit later
    TEST_ASSERT_FALSE(sender.waiting());
}

// RoofRoof's own park under way: the next request (the gap later) finds it
// slewing and waits; parked, nothing more. A request just after our own send
// waits for a reading the settle time later, even one that doesn't read slewing yet.
void test_our_own_park_is_never_asked_twice(void)
{
    ParkSender sender;
    sender.request(1000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, sender.step(at(1000, good(1000, false, false))));
    // 2 s later, another request; the mount hasn't started to read slewing.
    TEST_ASSERT_TRUE(sender.request(3000));
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(3000, good(3000, false, false))));
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(9000, good(9000, false, false))));
    // From 10 s after the send it reads slewing (parking): wait; then parked.
    TEST_ASSERT_EQUAL(ParkSendEvent::Waiting, sender.step(at(11000, good(11000, true, false))));
    TEST_ASSERT_EQUAL(0, run(sender, 13000, 70000, {50000, true}));
    TEST_ASSERT_FALSE(sender.waiting());
    // The 30 s re-ask, mid-park.
    ParkSender again;
    again.request(1000);
    again.step(at(1000, good(1000, false, false)));
    again.request(31000);
    ParkSendEvent last = ParkSendEvent::None;
    TEST_ASSERT_EQUAL(0, run(again, 31000, 120000, {60000, true}, &last));
    TEST_ASSERT_EQUAL(ParkSendEvent::AlreadyParked, last);
}

// Spec 043: the roof leaves its end during the wait: nothing sent, the wait ends.
void test_roof_leaving_its_end_drops_the_request(void)
{
    ParkSender sender;
    sender.request(1000);
    sender.step(at(1000, good(1000, true, false)));
    TEST_ASSERT_EQUAL(ParkSendEvent::Dropped, sender.step(at(3000, good(3000, true, false), false)));
    TEST_ASSERT_FALSE(sender.waiting());
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(5000, good(5000, false, false))));
    // Not at an end even when the slew has ended: still nothing sent.
    sender.request(7000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Dropped, sender.step(at(7000, good(7000, false, false), false)));
    // And at the limit.
    sender.request(10000);
    const uint64_t limitAt = 10000 + PARK_SLEW_WAIT_MS;
    TEST_ASSERT_EQUAL(ParkSendEvent::Dropped, sender.step(at(limitAt, good(limitAt, true, false), false)));
}

// A reading taken before the request (it may predate N.I.N.A.'s own park)
// decides nothing.
void test_a_reading_from_before_the_request_decides_nothing(void)
{
    ParkSender sender;
    sender.request(5000);
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(5000, good(4000, false, false))));
    TEST_ASSERT_TRUE(sender.waiting());
    TEST_ASSERT_FALSE(sender.waitingForSlew());
    TEST_ASSERT_EQUAL(ParkSendEvent::Waiting, sender.step(at(5100, good(5100, true, false))));
}

// N.I.N.A. unreadable: sent as before, unless the last good reading is fresh and slewing.
void test_unreadable_nina(void)
{
    ParkSender sender;
    sender.request(1000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, sender.step(at(1000, failed(1000))));

    ParkSender slewing;
    slewing.step(at(1000, good(1000, true, false))); // seen before any request
    slewing.request(2000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Waiting, slewing.step(at(3000, failed(3000))));
    TEST_ASSERT_EQUAL(ParkSendEvent::None, slewing.step(at(1000 + NINA_FRESH_MS, failed(1000 + NINA_FRESH_MS))));
    // The last good reading past its freshness: sent.
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, slewing.step(at(1001 + NINA_FRESH_MS, failed(1001 + NINA_FRESH_MS))));

    ParkSender still;
    still.step(at(1000, good(1000, false, false)));
    still.request(2000);
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, still.step(at(3000, failed(3000))));
}

// No address: as before (sent at once; the sender records it as refused).
void test_no_address_sends_at_once(void)
{
    ParkSender sender;
    sender.request(1000);
    ParkSendInputs in = at(1000, Nina::failedReading(Nina::ReadingStatus::NotConfigured, "", 0));
    in.ninaSet = false;
    TEST_ASSERT_EQUAL(ParkSendEvent::Send, sender.step(in));
}

void test_reset_forgets_a_waiting_request(void)
{
    ParkSender sender;
    sender.request(1000);
    sender.step(at(1000, good(1000, true, false)));
    sender.reset();
    TEST_ASSERT_FALSE(sender.waiting());
    TEST_ASSERT_EQUAL(ParkSendEvent::None, sender.step(at(3000, good(3000, false, false))));
}

void test_the_park_text_says_why_nothing_was_sent(void)
{
    ParkResult r;
    r.reasons = ParkMountNotAtPark | ParkMountSlewing;
    TEST_ASSERT_EQUAL_STRING("not parked: N.I.N.A. says mount not at park; Mount slewing", describe(r).c_str());
    r.askWaiting = true;
    TEST_ASSERT_EQUAL_STRING(
        "not parked: N.I.N.A. says mount not at park; Mount slewing; waiting for the mount to stop slewing "
        "before asking N.I.N.A. to park",
        describe(r).c_str());
}

// The wait's limit leaves the park time to be confirmed within the park time
// limit's default.
void test_the_limit_is_below_the_park_time_limit(void)
{
    TEST_ASSERT_TRUE(PARK_SLEW_WAIT_MS < 240000);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_slewing_then_parked_sends_nothing);
    RUN_TEST(test_slewing_then_stopped_sends_once);
    RUN_TEST(test_already_at_park_sends_nothing);
    RUN_TEST(test_still_and_not_parked_sends_at_once);
    RUN_TEST(test_at_the_limit_the_slew_is_stopped_then_parked_when_still);
    RUN_TEST(test_still_slewing_after_the_stop_sends_the_park_anyway);
    RUN_TEST(test_parked_after_the_stop_sends_nothing);
    RUN_TEST(test_roof_leaving_its_end_after_the_stop_sends_no_park);
    RUN_TEST(test_duplicate_requests_while_waiting_send_once);
    RUN_TEST(test_our_own_park_is_never_asked_twice);
    RUN_TEST(test_roof_leaving_its_end_drops_the_request);
    RUN_TEST(test_a_reading_from_before_the_request_decides_nothing);
    RUN_TEST(test_unreadable_nina);
    RUN_TEST(test_no_address_sends_at_once);
    RUN_TEST(test_reset_forgets_a_waiting_request);
    RUN_TEST(test_the_park_text_says_why_nothing_was_sent);
    RUN_TEST(test_the_limit_is_below_the_park_time_limit);
    return UNITY_END();
}
