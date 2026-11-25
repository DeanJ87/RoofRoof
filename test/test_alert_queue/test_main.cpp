// Spec 030 (FR-008, User Story 4): alerts survive outages. An alert no
// channel took is sent again with back-off and at once when the network is
// back, until a channel takes it, a newer one of the same event supersedes it
// or its condition is resolved; the queue is bounded; the critical ones are
// kept as records that survive a restart (version byte and checksum).
// Also spec 030 item 8: after a restart with N.I.N.A. silent, the close keeps
// being attempted and reminded.

#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "../close_rig/CloseRig.h"
#include "AlertQueue.h"
#include "Config.h" // for the library finder: RoofLogic builds with ConfigModel
#include "KeptAlert.h"
#include "RoofAlerts.h"

using namespace SQM;
using namespace SQM::CloseSeq;

void setUp(void) {}
void tearDown(void) {}

namespace
{
    Alerts::Alert alertOf(Alerts::AlertType type, const std::string &message)
    {
        Alerts::Alert alert;
        alert.type = type;
        alert.level = Alerts::AlertLevel::Wake;
        alert.title = "Title";
        alert.message = message;
        alert.vars = {{"reason", "why"}, {"details", message}};
        return alert;
    }

    // The dispatcher as the queue sees it: each send gets a record whose
    // outcome is set by the test (Delivered while `takes`, else Failed).
    struct Channels
    {
        bool takes = true;
        Delivery when = Delivery::Delivered; // what a channel that takes it says
        uint32_t nextId = 1;
        std::map<uint32_t, Delivery> outcome;
        std::vector<std::string> sent; // the messages sent, in order

        AlertQueue::Send send()
        {
            return [this](const QueuedAlert &queued)
            {
                sent.push_back(queued.alert.message);
                const uint32_t id = nextId++;
                outcome[id] = takes ? when : Delivery::Failed;
                return id;
            };
        }
        AlertQueue::Probe probe()
        {
            return [this](uint32_t id) { return outcome.count(id) ? outcome[id] : Delivery::Failed; };
        }
    };

    // Steps the queue every second from `fromMs` to `toMs`.
    void run(AlertQueue &queue, Channels &channels, bool up, uint64_t fromMs, uint64_t toMs)
    {
        for (uint64_t t = fromMs; t <= toMs; t += 1000)
            queue.step(t, up, channels.send(), channels.probe());
    }
} // namespace

void test_delivery_from_the_channels(void)
{
    TEST_ASSERT_EQUAL(Delivery::Delivered, deliveryFrom(true, true, true));
    TEST_ASSERT_EQUAL(Delivery::Pending, deliveryFrom(false, true, true));
    // Skipped for now (no network, the broker not connected) is retried: the
    // field's silent "can't move" was such a skip read as "no channel".
    TEST_ASSERT_EQUAL(Delivery::Failed, deliveryFrom(false, false, true));
    TEST_ASSERT_EQUAL(Delivery::NoChannel, deliveryFrom(false, false, false));
}

// AP down -> the alert fails -> retried with back-off -> network back -> delivered once.
void test_an_outage_backs_off_and_delivers_once_when_the_network_is_back(void)
{
    AlertQueue queue;
    Channels channels;
    channels.takes = false; // the access point is up but nothing gets through
    queue.push(alertOf(Alerts::AlertType::RoofNotClosed, "m"), true);
    run(queue, channels, true, 0, 59000);
    TEST_ASSERT_EQUAL(1, channels.sent.size()); // at once, then waits the first gap
    run(queue, channels, true, 60000, 60000);
    TEST_ASSERT_EQUAL(2, channels.sent.size()); // 60 s
    run(queue, channels, true, 61000, 179000);
    TEST_ASSERT_EQUAL(2, channels.sent.size());
    run(queue, channels, true, 180000, 180000);
    TEST_ASSERT_EQUAL(3, channels.sent.size()); // then 120 s later
    // The access point goes down: nothing is sent at all.
    run(queue, channels, false, 181000, 900000);
    TEST_ASSERT_EQUAL(3, channels.sent.size());
    TEST_ASSERT_EQUAL(1, queue.size());
    // Back: sent at once, taken, and never again.
    channels.takes = true;
    run(queue, channels, true, 901000, 901000);
    TEST_ASSERT_EQUAL(4, channels.sent.size());
    run(queue, channels, true, 902000, 4000000);
    TEST_ASSERT_EQUAL(4, channels.sent.size());
    TEST_ASSERT_EQUAL(0, queue.size());
}

void test_the_gap_doubles_up_to_the_reminder_interval(void)
{
    AlertQueue queue;
    queue.setMaxGap(240000);
    Channels channels;
    channels.takes = false;
    queue.push(alertOf(Alerts::AlertType::NeedsPerson, "m"), true);
    run(queue, channels, true, 0, 0);
    // 60, 120, 240, 240, 240 s apart.
    run(queue, channels, true, 1000, 60000 + 120000 + 240000 * 3);
    TEST_ASSERT_EQUAL(6, channels.sent.size());
}

void test_a_send_still_pending_waits_then_counts_as_failed(void)
{
    AlertQueue queue;
    Channels channels;
    channels.when = Delivery::Pending;
    queue.push(alertOf(Alerts::AlertType::MountNotParked, "m"), true);
    run(queue, channels, true, 0, ALERT_PENDING_STALE_MS - 1000);
    TEST_ASSERT_EQUAL(1, channels.sent.size());
    run(queue, channels, true, ALERT_PENDING_STALE_MS, ALERT_PENDING_STALE_MS);
    TEST_ASSERT_EQUAL(2, channels.sent.size());
}

void test_no_channel_able_to_take_it_drops_it(void)
{
    AlertQueue queue;
    Channels channels;
    channels.when = Delivery::NoChannel;
    queue.push(alertOf(Alerts::AlertType::MoveFault, "m"), false);
    run(queue, channels, true, 0, 600000);
    TEST_ASSERT_EQUAL(1, channels.sent.size());
    TEST_ASSERT_EQUAL(0, queue.size());
}

void test_a_newer_alert_of_the_same_event_supersedes_the_waiting_one(void)
{
    AlertQueue queue;
    Channels channels;
    const uint32_t first = queue.push(alertOf(Alerts::AlertType::NeedsPerson, "old"), true);
    queue.push(alertOf(Alerts::AlertType::MountNotParked, "park"), true);
    const uint32_t second = queue.push(alertOf(Alerts::AlertType::NeedsPerson, "new"), true);
    TEST_ASSERT_TRUE(second != first);
    TEST_ASSERT_EQUAL(2, queue.size());
    TEST_ASSERT_EQUAL_STRING("new", queue.find(Alerts::AlertType::NeedsPerson)->alert.message.c_str());
    TEST_ASSERT_EQUAL(second, queue.find(Alerts::AlertType::NeedsPerson)->serial);
    run(queue, channels, false, 0, 0);
    run(queue, channels, true, 1000, 2000); // spec 035: one send per step
    TEST_ASSERT_EQUAL(2, channels.sent.size());
    TEST_ASSERT_EQUAL_STRING("park", channels.sent[0].c_str());
    TEST_ASSERT_EQUAL_STRING("new", channels.sent[1].c_str());
}

void test_resolve_drops_only_a_condition_that_ended(void)
{
    AlertQueue queue;
    queue.push(alertOf(Alerts::AlertType::RoofNotClosed, "c"), true);
    queue.push(alertOf(Alerts::AlertType::MountNotParked, "a person's wait gave up"), false);
    queue.resolve(Alerts::AlertType::RoofNotClosed);
    queue.resolve(Alerts::AlertType::MountNotParked);
    TEST_ASSERT_EQUAL(1, queue.size());
    TEST_ASSERT_NULL(queue.find(Alerts::AlertType::RoofNotClosed));
    TEST_ASSERT_NOT_NULL(queue.find(Alerts::AlertType::MountNotParked));
}

void test_a_full_queue_drops_the_oldest_non_critical_alert(void)
{
    AlertQueue queue;
    queue.push(alertOf(Alerts::AlertType::MountNotParked, "1"), true);
    queue.push(alertOf(Alerts::AlertType::MoveFault, "2"), false);
    queue.push(alertOf(Alerts::AlertType::RoofNotClosed, "3"), true);
    queue.push(alertOf(Alerts::AlertType::SupplyLow, "4"), false);
    queue.push(alertOf(Alerts::AlertType::NeedsPerson, "5"), true);
    queue.push(alertOf(Alerts::AlertType::SafetyLost, "6"), false);
    TEST_ASSERT_EQUAL(ALERT_QUEUE_CAPACITY, queue.size());
    queue.push(alertOf(Alerts::AlertType::NinaLost, "7"), false);
    TEST_ASSERT_EQUAL(ALERT_QUEUE_CAPACITY, queue.size());
    TEST_ASSERT_NULL(queue.find(Alerts::AlertType::MoveFault));
    for (Alerts::AlertType type :
         {Alerts::AlertType::MountNotParked, Alerts::AlertType::RoofNotClosed, Alerts::AlertType::NeedsPerson, Alerts::AlertType::NinaLost})
        TEST_ASSERT_NOT_NULL(queue.find(type));
    // Superseding a waiting one makes no room: nothing else goes.
    queue.push(alertOf(Alerts::AlertType::SupplyLow, "4b"), false);
    TEST_ASSERT_NOT_NULL(queue.find(Alerts::AlertType::SafetyLost));
}

void test_a_kept_alert_round_trips(void)
{
    KeptAlert kept;
    kept.alert = alertOf(Alerts::AlertType::NeedsPerson, "Position unknown: the Hall sensor reads between (2050 counts).");
    kept.alert.vars.push_back({"reminder", "no"});
    kept.queuedAt = 1764100000u;
    const std::vector<uint8_t> bytes = encodeKept(kept);
    TEST_ASSERT_EQUAL(KEPT_ALERT_VERSION, bytes[0]);
    KeptAlert back;
    TEST_ASSERT_TRUE(decodeKept(bytes.data(), bytes.size(), back));
    TEST_ASSERT_EQUAL(Alerts::AlertType::NeedsPerson, back.alert.type);
    TEST_ASSERT_EQUAL(Alerts::AlertLevel::Wake, back.alert.level);
    TEST_ASSERT_EQUAL_STRING(kept.alert.title.c_str(), back.alert.title.c_str());
    TEST_ASSERT_EQUAL_STRING(kept.alert.message.c_str(), back.alert.message.c_str());
    TEST_ASSERT_EQUAL(1764100000u, back.queuedAt);
    TEST_ASSERT_EQUAL(3, back.alert.vars.size());
    TEST_ASSERT_EQUAL_STRING("reminder", back.alert.vars[2].first.c_str());
    TEST_ASSERT_EQUAL_STRING("no", back.alert.vars[2].second.c_str());
    // Too long texts are cut, never refused.
    kept.alert.message = std::string(3000, 'x');
    TEST_ASSERT_TRUE(decodeKept(encodeKept(kept).data(), encodeKept(kept).size(), back));
    TEST_ASSERT_EQUAL(KEPT_ALERT_MAX_TEXT, back.alert.message.size());
}

void test_an_unreadable_kept_record_is_dropped(void)
{
    KeptAlert kept;
    kept.alert = alertOf(Alerts::AlertType::RoofNotClosed, "Roof didn't close");
    std::vector<uint8_t> bytes = encodeKept(kept);
    KeptAlert back;
    std::vector<uint8_t> corrupt = bytes;
    corrupt[12] ^= 0x01;
    TEST_ASSERT_FALSE(decodeKept(corrupt.data(), corrupt.size(), back));
    TEST_ASSERT_FALSE(decodeKept(bytes.data(), bytes.size() - 1, back)); // cut short
    std::vector<uint8_t> other = bytes;
    other[0] = KEPT_ALERT_VERSION + 1;
    TEST_ASSERT_FALSE(decodeKept(other.data(), other.size(), back));
    TEST_ASSERT_FALSE(decodeKept(nullptr, 0, back));
    TEST_ASSERT_FALSE(decodeKept(bytes.data(), 3, back));
}

void test_a_kept_alert_sent_after_a_restart_says_so(void)
{
    const Alerts::Alert alert = alertOf(Alerts::AlertType::MountNotParked, "Mount not parked.");
    const Alerts::Alert marked = markRestarted(alert, "2026-11-25 02:14");
    TEST_ASSERT_EQUAL_STRING("Mount not parked.\n\nSent before a restart at 2026-11-25 02:14.", marked.message.c_str());
    TEST_ASSERT_EQUAL_STRING(marked.message.c_str(), marked.vars[1].second.c_str()); // {details}
    TEST_ASSERT_EQUAL_STRING("Mount not parked.\n\nSent before a restart.", markRestarted(alert, "").message.c_str());
}

// A kept record queued again after a restart is delivered once the network
// is up; a condition still present raised afresh supersedes it.
void test_a_restart_queues_the_kept_alert_and_a_fresh_one_supersedes_it(void)
{
    KeptAlert kept;
    kept.alert = alertOf(Alerts::AlertType::NeedsPerson, "before");
    const std::vector<uint8_t> bytes = encodeKept(kept);
    AlertQueue queue;
    KeptAlert back;
    TEST_ASSERT_TRUE(decodeKept(bytes.data(), bytes.size(), back));
    queue.push(markRestarted(back.alert, ""), false);
    Channels channels;
    run(queue, channels, false, 0, 5000); // no network yet after the restart
    TEST_ASSERT_EQUAL(0, channels.sent.size());
    run(queue, channels, true, 6000, 10000);
    TEST_ASSERT_EQUAL(1, channels.sent.size());
    TEST_ASSERT_EQUAL_STRING("before\n\nSent before a restart.", channels.sent[0].c_str());
    TEST_ASSERT_EQUAL(0, queue.size());
    queue.push(markRestarted(back.alert, ""), false);
    queue.push(alertOf(Alerts::AlertType::NeedsPerson, "now"), true);
    TEST_ASSERT_EQUAL(1, queue.size());
    TEST_ASSERT_EQUAL_STRING("now", queue.find(Alerts::AlertType::NeedsPerson)->alert.message.c_str());
}

namespace
{
    // The main loop's glue around the close sequence (CloseOnUnsafe and the
    // alert outbox): a critical decision is queued, the sequence going idle
    // resolves its alerts, the queue steps every second.
    struct Story
    {
        CloseTest::Rig rig;
        AlertQueue queue;
        Channels channels;
        bool up = false;
        CloseTest::Counts counts;

        void run(uint64_t toMs)
        {
            while (rig.in.nowMs + 100 <= toMs)
            {
                const Decision d = rig.at(rig.in.nowMs + 100);
                CloseTest::Rig::note(counts, d);
                if (d.action == Action::Close)
                {
                    rig.seq.closeAnswered(CloseAnswer::Started);
                    rig.in.roof = Roof::State::Closing;
                    rig.in.moveTimeoutMs = 60000;
                }
                if (isCritical(d.event) && d.alert)
                    queue.push(criticalAlert(d, "not parked: N.I.N.A. unreachable"), true);
                if (rig.seq.phase() == Phase::Idle)
                {
                    queue.resolve(Alerts::AlertType::MountNotParked);
                    queue.resolve(Alerts::AlertType::RoofNotClosed);
                }
                if (rig.in.nowMs % 1000 == 0)
                    queue.step(rig.in.nowMs, up, channels.send(), channels.probe());
            }
        }

        // The access point down: SQMeter stale (unsafe), N.I.N.A. unreachable.
        void outage()
        {
            using namespace CloseTest;
            up = false;
            rig.in.weatherSafe = false;
            rig.in.parked = false;
            run(T0 + 600000); // 10 minutes: the park limit ran out, the back-off began
            TEST_ASSERT_TRUE(counts.requests >= 2);
            TEST_ASSERT_EQUAL(0, channels.sent.size());
            TEST_ASSERT_NOT_NULL(queue.find(Alerts::AlertType::MountNotParked));
        }
    };
} // namespace

// User Story 4: AP down -> SQMeter stale -> close wanted, N.I.N.A. unreachable ->
// retrying and the alert queued -> AP up, weather safe -> the close is called
// off and the queued alert delivered.
void test_an_outage_story_ends_with_the_close_called_off_and_the_alert_delivered(void)
{
    using namespace CloseTest;
    Story story;
    story.outage();
    story.up = true; // the access point is back: the alert goes out at once
    story.run(T0 + 602000);
    TEST_ASSERT_EQUAL(1, story.channels.sent.size());
    TEST_ASSERT_TRUE(story.channels.sent[0].find("N.I.N.A. unreachable") != std::string::npos);
    story.rig.in.weatherSafe = true; // SQMeter answers again, safe (after the hold's clear spell)
    story.run(T0 + 662000);
    TEST_ASSERT_EQUAL(Phase::Idle, story.rig.seq.phase());
    story.run(T0 + 3600000);
    TEST_ASSERT_EQUAL(1, story.channels.sent.size()); // delivered once
    TEST_ASSERT_EQUAL(0, story.queue.size());
    TEST_ASSERT_EQUAL(0, story.counts.closes);
}

// The unsafe variant: the network and N.I.N.A. come back while it stays
// unsafe: the alert is delivered, the mount parks and the roof closes.
void test_an_outage_story_in_unsafe_weather_ends_parked_and_closed(void)
{
    using namespace CloseTest;
    Story story;
    story.outage();
    story.up = true;
    story.rig.in.parked = true; // N.I.N.A. answers: parked
    story.run(T0 + 610000);     // the park holds for its 4 s: the close is commanded
    TEST_ASSERT_EQUAL(1, story.counts.closes);
    story.rig.in.roof = Roof::State::Closed;
    story.run(T0 + 620000);
    TEST_ASSERT_EQUAL(Phase::Idle, story.rig.seq.phase());
    TEST_ASSERT_EQUAL(1, story.channels.sent.size());
    TEST_ASSERT_EQUAL(0, story.queue.size());
}

// Spec 030 item 8: a sequence restarted with N.I.N.A. silent for longer than
// park.timeoutSeconds keeps attempting (RetryWait -> attempt -> ...) and reminding.
void test_a_restart_with_nina_silent_keeps_attempting_and_reminding(void)
{
    using namespace CloseTest;
    Sequence seq;
    Sequence::Saved saved;
    saved.phase = Phase::WaitPark;
    saved.unsafeMs = DEFAULT_PARK_TIMEOUT_MS + 60000; // N.I.N.A. silent longer than the limit
    saved.wallMs = saved.unsafeMs;
    saved.limitMs = DEFAULT_PARK_TIMEOUT_MS;
    saved.requested = true;
    saved.requestAgeMs = 20000;
    seq.restore(saved);
    Inputs in;
    in.weatherSafe = false;
    in.roof = Roof::State::Open;
    in.parked = false;
    Counts counts;
    for (in.nowMs = T0; in.nowMs <= T0 + 3 * 3600000ull; in.nowMs += 1000)
        Rig::note(counts, seq.step(in));
    TEST_ASSERT_TRUE(seq.phase() == Phase::WaitPark || seq.phase() == Phase::RetryWait);
    // An attempt every back-off and park limit (5 + 4 min): about 20 in 3 h.
    int started = 0, failed = 0, alerted = 0;
    for (const Decision &d : counts.events)
    {
        started += d.event == Event::Started;
        failed += d.event == Event::ParkNotConfirmed;
        alerted += d.alert;
    }
    TEST_ASSERT_TRUE(started >= 15);
    TEST_ASSERT_TRUE(failed >= 15);
    TEST_ASSERT_TRUE(counts.requests >= 15);
    // The first failure alerts at once (after the restart), then once per reminder interval.
    TEST_ASSERT_TRUE(alerted >= 6 && alerted <= 8);
    TEST_ASSERT_EQUAL_STRING(eventName(Event::ParkNotConfirmed), eventName(counts.events[0].event));
    TEST_ASSERT_TRUE(counts.events[0].alert);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_delivery_from_the_channels);
    RUN_TEST(test_an_outage_backs_off_and_delivers_once_when_the_network_is_back);
    RUN_TEST(test_the_gap_doubles_up_to_the_reminder_interval);
    RUN_TEST(test_a_send_still_pending_waits_then_counts_as_failed);
    RUN_TEST(test_no_channel_able_to_take_it_drops_it);
    RUN_TEST(test_a_newer_alert_of_the_same_event_supersedes_the_waiting_one);
    RUN_TEST(test_resolve_drops_only_a_condition_that_ended);
    RUN_TEST(test_a_full_queue_drops_the_oldest_non_critical_alert);
    RUN_TEST(test_a_kept_alert_round_trips);
    RUN_TEST(test_an_unreadable_kept_record_is_dropped);
    RUN_TEST(test_a_kept_alert_sent_after_a_restart_says_so);
    RUN_TEST(test_a_restart_queues_the_kept_alert_and_a_fresh_one_supersedes_it);
    RUN_TEST(test_an_outage_story_ends_with_the_close_called_off_and_the_alert_delivered);
    RUN_TEST(test_an_outage_story_in_unsafe_weather_ends_parked_and_closed);
    RUN_TEST(test_a_restart_with_nina_silent_keeps_attempting_and_reminding);
    return UNITY_END();
}
