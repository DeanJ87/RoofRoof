#include <unity.h>

#include "ConnectionBudget.h"
#include "HeapGuard.h"
#include "NetworkReturn.h"

#include <cstdint>
#include <cstring>

// Spec 048: the schedule after the network comes back (NetworkReturn,
// inbound(), MqttRetry, the alert pacing) and the record of each return.

namespace Budget = SQM::ConnectionBudget;
using Budget::DiscoveryOutcome;
using Budget::Inbound;
using Budget::NetworkReturn;
using Budget::ReturnKind;

void test_the_first_time_up_opens_a_window_but_is_not_a_return()
{
    NetworkReturn net;
    net.update(false, 100, 0);
    TEST_ASSERT_FALSE(net.online());
    TEST_ASSERT_EQUAL_UINT32(0, net.sinceReturn(100));
    net.update(true, 1000, 0);
    TEST_ASSERT_TRUE(net.inWindow(1000));
    TEST_ASSERT_EQUAL_UINT32(0, net.returns());
    TEST_ASSERT_EQUAL(1, net.size());
    TEST_ASSERT_EQUAL(ReturnKind::FirstUp, net.at(0).kind);
    TEST_ASSERT_TRUE(net.inWindow(1000 + Budget::RETURN_WINDOW_MS - 1));
    TEST_ASSERT_FALSE(net.inWindow(1000 + Budget::RETURN_WINDOW_MS));
}

void test_a_link_that_comes_back_is_a_return_with_its_outage()
{
    NetworkReturn net;
    net.update(true, 1000, 0);
    net.update(false, 50000, 0);
    TEST_ASSERT_FALSE(net.inWindow(50000));
    net.update(false, 100000, 0);
    net.update(true, 140000, 0);
    TEST_ASSERT_EQUAL_UINT32(1, net.returns());
    TEST_ASSERT_EQUAL(ReturnKind::LinkBack, net.at(0).kind);
    TEST_ASSERT_EQUAL_UINT32(90000, net.at(0).downMs);
    TEST_ASSERT_EQUAL_UINT32(140000, net.at(0).atMs);
    TEST_ASSERT_EQUAL_UINT32(5000, net.sinceReturn(145000));
    TEST_ASSERT_EQUAL_UINT32(5000, net.sinceLinkUp(145000));
}

// An access point that loses its uplink may keep its clients: the link
// stays, the requests stop. A client after QUIET_RETURN_MS is a return.
void test_requests_after_a_long_silence_are_a_return()
{
    NetworkReturn net;
    net.update(true, 1000, 0);
    net.update(true, 2000, 2000); // the first client since start: not a return
    TEST_ASSERT_EQUAL_UINT32(0, net.returns());
    net.update(true, 3000, 2900); // steady polling
    net.update(true, 3000 + Budget::QUIET_RETURN_MS - 1, 2900 + Budget::QUIET_RETURN_MS - 1);
    TEST_ASSERT_EQUAL_UINT32(0, net.returns()); // just short of the silence
    const uint32_t back = 3000 + 2 * Budget::QUIET_RETURN_MS + 5000;
    net.update(true, back, back);
    TEST_ASSERT_EQUAL_UINT32(1, net.returns());
    TEST_ASSERT_EQUAL(ReturnKind::RequestsBack, net.at(0).kind);
    TEST_ASSERT_EQUAL_UINT32(back - (2900 + Budget::QUIET_RETURN_MS - 1), net.at(0).downMs);
    TEST_ASSERT_TRUE(net.inWindow(back));
}

void test_a_silence_right_after_the_link_came_back_is_not_counted_twice()
{
    NetworkReturn net;
    net.update(true, 1000, 500);
    net.update(false, 2000, 500);
    net.update(true, 100000, 500);    // LinkBack
    net.update(true, 100500, 100400); // its first client: the same return
    TEST_ASSERT_EQUAL_UINT32(1, net.returns());
    TEST_ASSERT_EQUAL(ReturnKind::LinkBack, net.at(0).kind);
}

void test_the_record_collects_the_lows_after_a_return_only()
{
    NetworkReturn net;
    net.update(true, 1000, 0);
    net.update(false, 2000, 0);
    net.update(true, 10000, 0);
    net.noteHeap(10100, 30000, 50000);
    net.noteHeap(12000, 9000, 20000);
    net.noteHeap(13000, 20000, 15000);
    net.noteInbound(10200, 2, 4);
    net.noteInbound(10300, 3, 7);
    net.noteMqttConnected(20000);
    net.noteMqttConnected(40000); // the first connect counts
    net.noteDiscovery(DiscoveryOutcome::Skipped);
    net.noteDiscovery(DiscoveryOutcome::Published); // the first outcome counts
    // Past the follow-up: not this return's any more.
    net.noteHeap(10000 + Budget::RETURN_FOLLOW_MS, 100, 100);
    const Budget::ReturnRecord &r = net.at(0);
    TEST_ASSERT_EQUAL_UINT32(9000, r.lowestLargest);
    TEST_ASSERT_EQUAL_UINT32(15000, r.lowestFree);
    TEST_ASSERT_EQUAL_UINT32(7, r.refused); // the gate's count was 0 at the return
    TEST_ASSERT_EQUAL_UINT16(3, r.peakLive);
    TEST_ASSERT_UINT32_WITHIN(1, 10000, r.mqttAfterMs);
    TEST_ASSERT_EQUAL(DiscoveryOutcome::Skipped, r.discovery);
}

void test_refusals_before_a_return_are_not_its_own()
{
    NetworkReturn net;
    net.update(true, 1000, 0);
    net.noteInbound(2000, 1, 5);
    net.update(false, 3000, 0);
    net.update(true, 9000, 0);
    net.noteInbound(9100, 1, 6);
    TEST_ASSERT_EQUAL_UINT32(1, net.at(0).refused);
}

void test_the_ring_keeps_the_newest_returns()
{
    NetworkReturn net;
    uint32_t t = 1000;
    for (int i = 0; i < 12; ++i)
    {
        net.update(true, t, 0);
        t += 1000;
        net.update(false, t, 0);
        t += 1000;
    }
    TEST_ASSERT_EQUAL(NetworkReturn::KEPT, net.size());
    TEST_ASSERT_EQUAL_UINT32(11, net.returns());
    TEST_ASSERT_EQUAL_UINT32(t - 2000, net.at(0).atMs);
    TEST_ASSERT_EQUAL_UINT32(t - 2000 - 2000 * (NetworkReturn::KEPT - 1), net.at(NetworkReturn::KEPT - 1).atMs);
}

void test_the_window_holds_across_the_millis_wrap()
{
    NetworkReturn net;
    net.update(true, 0xFFFFF000u, 0);
    TEST_ASSERT_TRUE(net.inWindow(0x00000F00u));
    TEST_ASSERT_EQUAL_UINT32(0x1F00u, net.sinceReturn(0x00000F00u));
}

void test_inbound_connections_are_capped_harder_in_a_window()
{
    using Budget::ReturnPhase;
    // Settled: up to the TCP budget's share for HTTP, whatever the heap.
    TEST_ASSERT_EQUAL(Inbound::Accept, Budget::inbound(Budget::MAX_LIVE_INBOUND - 1, 1000, ReturnPhase::Settled));
    TEST_ASSERT_EQUAL(Inbound::Busy, Budget::inbound(Budget::MAX_LIVE_INBOUND, 60000, ReturnPhase::Settled));
    TEST_ASSERT_TRUE(Budget::MAX_LIVE_INBOUND <= Budget::httpReserve());
    // The first is always taken.
    TEST_ASSERT_EQUAL(Inbound::Accept, Budget::inbound(0, 0, ReturnPhase::Window));
    TEST_ASSERT_EQUAL(Inbound::Accept, Budget::inbound(0, 0, ReturnPhase::Following));
    // In the window: more only with room, never past RETURN_LIVE_INBOUND.
    TEST_ASSERT_EQUAL(Inbound::Accept, Budget::inbound(1, Budget::RETURN_ACCEPT_BLOCK_BYTES, ReturnPhase::Window));
    TEST_ASSERT_EQUAL(Inbound::Busy, Budget::inbound(1, Budget::RETURN_ACCEPT_BLOCK_BYTES - 1, ReturnPhase::Window));
    TEST_ASSERT_EQUAL(Inbound::Busy, Budget::inbound(Budget::RETURN_LIVE_INBOUND, 60000, ReturnPhase::Window));
    // Then up to four with room.
    TEST_ASSERT_EQUAL(Inbound::Accept, Budget::inbound(Budget::FOLLOW_LIVE_INBOUND - 1, 60000, ReturnPhase::Following));
    TEST_ASSERT_EQUAL(Inbound::Busy, Budget::inbound(1, Budget::RETURN_ACCEPT_BLOCK_BYTES - 1, ReturnPhase::Following));
    TEST_ASSERT_EQUAL(Inbound::Busy, Budget::inbound(Budget::FOLLOW_LIVE_INBOUND, 60000, ReturnPhase::Following));
}

void test_the_phase_follows_the_return()
{
    using Budget::ReturnPhase;
    NetworkReturn net;
    TEST_ASSERT_EQUAL(ReturnPhase::Settled, net.phase(0)); // never up
    net.update(true, 1000, 0);
    TEST_ASSERT_EQUAL(ReturnPhase::Window, net.phase(1000));
    TEST_ASSERT_EQUAL(ReturnPhase::Following, net.phase(1000 + Budget::RETURN_WINDOW_MS));
    TEST_ASSERT_EQUAL(ReturnPhase::Settled, net.phase(1000 + Budget::RETURN_FOLLOW_MS));
}

void test_one_built_reply_at_a_time_in_a_window()
{
    TEST_ASSERT_EQUAL_UINT32(SQM::HeapSafe::MAX_REPLIES_IN_FLIGHT, Budget::repliesAllowed(false, SQM::HeapSafe::MAX_REPLIES_IN_FLIGHT));
    TEST_ASSERT_EQUAL_UINT32(1, Budget::repliesAllowed(true, SQM::HeapSafe::MAX_REPLIES_IN_FLIGHT));
    // The cap the guard applies with a smaller limit.
    TEST_ASSERT_TRUE(SQM::HeapSafe::replySlotFree("/api/status", false, 0, 1));
    TEST_ASSERT_FALSE(SQM::HeapSafe::replySlotFree("/api/status", false, 1, 1));
    TEST_ASSERT_TRUE(SQM::HeapSafe::replySlotFree("/api/v1/dome/0/shutterstatus", false, 5, 1)); // Alpaca: never
}

void test_mqtt_waits_for_the_return_and_backs_off()
{
    Budget::MqttRetry retry;
    TEST_ASSERT_FALSE(retry.due(0, false, 0)); // down
    TEST_ASSERT_FALSE(retry.due(1000, true, Budget::MQTT_RETURN_DELAY_MS - 1));
    TEST_ASSERT_TRUE(retry.due(1000, true, Budget::MQTT_RETURN_DELAY_MS));
    retry.finished(1000, false); // the next one 10 s after this one ended
    TEST_ASSERT_FALSE(retry.due(1000 + 9999, true, 60000));
    TEST_ASSERT_TRUE(retry.due(1000 + 10000, true, 60000));
    retry.finished(11000, false);
    TEST_ASSERT_EQUAL_UINT32(20000, retry.intervalMs());
    retry.finished(26000, false);
    retry.finished(56000, false);
    retry.finished(86000, false);
    TEST_ASSERT_EQUAL_UINT32(Budget::MQTT_RETRY_MAX_MS, retry.intervalMs());
    retry.networkReturned();
    TEST_ASSERT_EQUAL_UINT32(Budget::MQTT_RETRY_MS, retry.intervalMs());
    retry.finished(90000, true);
    TEST_ASSERT_EQUAL_UINT32(Budget::MQTT_RETRY_MS, retry.intervalMs());
}

void test_background_work_needs_more_room_after_a_return()
{
    TEST_ASSERT_TRUE(Budget::backgroundFits(Budget::BACKGROUND_MIN_BLOCK_BYTES, false));
    TEST_ASSERT_FALSE(Budget::backgroundFits(Budget::BACKGROUND_MIN_BLOCK_BYTES - 1, false));
    TEST_ASSERT_FALSE(Budget::backgroundFits(Budget::RETURN_ACCEPT_BLOCK_BYTES - 1, true));
    TEST_ASSERT_TRUE(Budget::backgroundFits(Budget::RETURN_ACCEPT_BLOCK_BYTES, true));
    TEST_ASSERT_EQUAL_UINT32(SQM::HeapSafe::MIN_BLOCK_BYTES, Budget::BACKGROUND_MIN_BLOCK_BYTES);
}

void test_queued_alerts_go_slowly_after_a_return()
{
    TEST_ASSERT_TRUE(Budget::MQTT_RETURN_DELAY_MS < Budget::ALERT_RETURN_DELAY_MS);
    TEST_ASSERT_FALSE(Budget::alertsMayGo(true, Budget::ALERT_RETURN_DELAY_MS - 1));
    TEST_ASSERT_TRUE(Budget::alertsMayGo(true, Budget::ALERT_RETURN_DELAY_MS));
    TEST_ASSERT_TRUE(Budget::alertsMayGo(false, 0)); // down: the queue still steps (and holds them)
    TEST_ASSERT_EQUAL_UINT32(Budget::ALERT_RETURN_GAP_MS, Budget::alertGapMs(true, Budget::ALERT_RETURN_DELAY_MS));
    TEST_ASSERT_EQUAL_UINT32(Budget::ALERT_GAP_MS, Budget::alertGapMs(true, Budget::RETURN_FOLLOW_MS));
    TEST_ASSERT_EQUAL_UINT32(Budget::ALERT_GAP_MS, Budget::alertGapMs(false, 0));
}

void test_a_return_reads_as_one_line()
{
    NetworkReturn net;
    net.update(true, 1000, 0);
    net.update(false, 2318000, 0);
    net.update(true, 2439000, 0);
    net.noteHeap(2440000, 3316, 10540);
    net.noteInbound(2441000, 3, 12);
    net.noteMqttConnected(2449400);
    net.noteDiscovery(DiscoveryOutcome::Skipped);
    char line[256];
    const size_t n = Budget::describe(net.at(0), 2439000 + Budget::RETURN_FOLLOW_MS, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING(
        "  minute    40, uptime 2439 s: link back after 121 s; lowest largest 3316 B, free 10540 B; 12 busy at connect, "
        "at most 3 open; MQTT after 10.4 s; discovery kept by the broker",
        line);
    TEST_ASSERT_EQUAL(strlen(line), n);
    TEST_ASSERT_TRUE(Budget::returnedInMinute(net, 40));
    TEST_ASSERT_FALSE(Budget::returnedInMinute(net, 39));
    TEST_ASSERT_TRUE(Budget::returnedInMinute(net, 0)); // the first time up
    // Cut short, never past the buffer.
    char small[24];
    TEST_ASSERT_EQUAL(sizeof(small) - 1, Budget::describe(net.at(0), 2439000, small, sizeof(small)));
    // The first time up, still followed, before any reading.
    NetworkReturn first;
    first.update(true, 5000, 0);
    Budget::describe(first.at(0), 6000, line, sizeof(line));
    TEST_ASSERT_EQUAL_STRING(
        "  minute     0, uptime 5 s: first on a network; no heap reading yet; 0 busy at connect, at most 0 open; "
        "MQTT not connected since; discovery not sent yet (still following)",
        line);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_first_time_up_opens_a_window_but_is_not_a_return);
    RUN_TEST(test_a_link_that_comes_back_is_a_return_with_its_outage);
    RUN_TEST(test_requests_after_a_long_silence_are_a_return);
    RUN_TEST(test_a_silence_right_after_the_link_came_back_is_not_counted_twice);
    RUN_TEST(test_the_record_collects_the_lows_after_a_return_only);
    RUN_TEST(test_refusals_before_a_return_are_not_its_own);
    RUN_TEST(test_the_ring_keeps_the_newest_returns);
    RUN_TEST(test_the_window_holds_across_the_millis_wrap);
    RUN_TEST(test_inbound_connections_are_capped_harder_in_a_window);
    RUN_TEST(test_the_phase_follows_the_return);
    RUN_TEST(test_one_built_reply_at_a_time_in_a_window);
    RUN_TEST(test_mqtt_waits_for_the_return_and_backs_off);
    RUN_TEST(test_background_work_needs_more_room_after_a_return);
    RUN_TEST(test_queued_alerts_go_slowly_after_a_return);
    RUN_TEST(test_a_return_reads_as_one_line);
    return UNITY_END();
}
