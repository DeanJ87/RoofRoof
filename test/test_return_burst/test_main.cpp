#include <unity.h>

#include "HaDiscovery.h"
#include "HeapGuard.h"
#include "NetworkReturn.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// Spec 048 research R2: a model of the 90 s after the network comes back, at
// 10 ms steps. Each piece of work holds its bytes for its time; the model adds
// up what is held at once and runs the same arrivals twice: with spec 038's
// schedule (MQTT at 3 s with all discovery in one pass, alerts from 8 s one a
// second, every connection taken, two replies) and with spec 048's, which
// calls the real rules (NetworkReturn, inbound(), repliesAllowed(),
// MqttRetry, HaDiscovery::Pacer, the alert pacing).
//
// The costs are upper bounds from the earlier specs' measurements (031 R5,
// 037, 038 R5b, 045 R3b) and the library sources: a model for the order and
// overlap of the work, not a measurement. The board check is the soak
// (tools/soak/return_soak.py).

namespace Budget = SQM::ConnectionBudget;

namespace
{
    // Bytes held while each piece runs, and for how long (ms).
    constexpr uint32_t REST_FREE = 60000;    // heap8Bit free at rest (F51)
    constexpr uint32_t REST_LARGEST = 38900; // largest block at rest (F51)
    constexpr uint32_t ALPACA_BYTES = 2500;  // request object, PCB, reply, unacknowledged data
    constexpr uint32_t ALPACA_MS = 400;
    constexpr uint32_t STATUS_BYTES = 12000; // 3 KB pool, 5 KB pieces, 2.9 KB send buffer, request (045 R3b)
    constexpr uint32_t STATUS_MS = 800;
    constexpr uint32_t BUSY_BYTES = 1300; // a request answered "busy" by the guard
    constexpr uint32_t BUSY_MS = 100;
    constexpr uint32_t REFUSED_BYTES = 300; // answered 503 at accept: the PCB and one segment
    constexpr uint32_t REFUSED_MS = 50;
    constexpr uint32_t WS_HANDSHAKE_BYTES = 6000; // the upgrade request + first 4.4 KB frame unacknowledged
    constexpr uint32_t WS_HANDSHAKE_MS = 600;
    constexpr uint32_t WEATHER_BYTES = 10000; // HTTPClient, 1.4 KB receive buffer, body, parse
    constexpr uint32_t WEATHER_MS = 2000;
    constexpr uint32_t PARK_BYTES = 8000; // N.I.N.A.'s mount info, about 4 KB
    constexpr uint32_t PARK_MS = 1000;
    constexpr uint32_t MQTT_CONNECT_BYTES = 2500;
    constexpr uint32_t MQTT_CONNECT_MS = 300;
    constexpr uint32_t DISCOVERY_BURST_BYTES = 9000; // 2 KB pool + 5.7 KB unacknowledged + topics
    constexpr uint32_t DISCOVERY_BURST_MS = 1500;
    constexpr uint32_t DISCOVERY_ONE_BYTES = 3000; // 2 KB pool + one config unacknowledged
    constexpr uint32_t DISCOVERY_ONE_MS = 150;
    constexpr uint32_t ALERT_BYTES = 1800; // the record, the MQTT text, unacknowledged
    constexpr uint32_t ALERT_MS = 400;
    constexpr int QUEUED_ALERTS = 6; // F06's list after its outage
    constexpr uint32_t SIM_MS = 90000;
    constexpr uint32_t STEP_MS = 10;

    struct Held
    {
        uint32_t from;
        uint32_t until;
        uint32_t bytes;
        bool inbound; // counts as a live inbound connection
        bool reply;   // a built reply
    };

    // A client that wants a connection at `dueMs` (retried after a refusal).
    struct Client
    {
        uint32_t dueMs;
        uint32_t everyMs; // 0: once
        enum Kind : uint8_t
        {
            Alpaca,
            Status,
            WebSocket
        } kind;
        uint32_t retryMs;
    };

    struct Plan
    {
        bool spec048;
    };

    class Model
    {
    public:
        explicit Model(Plan plan)
            : plan(plan)
        {
            // N.I.N.A.: four devices' connections retrying at once, then
            // each polled about every 2 s (a request every 500 ms in all).
            for (int i = 0; i < 4; ++i)
                clients.push_back({static_cast<uint32_t>(i * 20), 2000, Client::Alpaca, 1000});
            // The harness's 1 Hz /api/status; two pages' live status and
            // their own status read.
            clients.push_back({0, 1000, Client::Status, 1000});
            for (int i = 0; i < 2; ++i)
            {
                clients.push_back({500u + static_cast<uint32_t>(i) * 100u, 0, Client::WebSocket, 1000});
                clients.push_back({700u + static_cast<uint32_t>(i) * 100u, 0, Client::Status, 2000});
            }
        }

        uint32_t run()
        {
            net.update(true, 1, 0);
            net.update(false, 2, 0);
            net.update(true, BACK_MS, 0);
            for (Client &c : clients)
                c.dueMs += BACK_MS;
            for (uint32_t t = BACK_MS; t < BACK_MS + SIM_MS; t += STEP_MS)
                step(t);
            return peak;
        }

        uint32_t refused = 0;
        uint32_t discoveryLast = 0;
        uint32_t alertsSent = 0;
        uint32_t mqttAt = 0;

    private:
        static constexpr uint32_t BACK_MS = 100000;

        Plan plan;
        Budget::NetworkReturn net;
        Budget::MqttRetry mqtt;
        SQM::HaDiscovery::Pacer pacer;
        std::vector<Held> held;
        std::vector<Client> clients;
        uint32_t peak = 0;
        uint32_t clock = 0;
        bool mqttUp = false;
        uint32_t nextWeather = 0;
        uint32_t nextAlert = 0;
        int alertsLeft = QUEUED_ALERTS;

        uint32_t inUse() const
        {
            uint32_t sum = 0;
            for (const Held &h : held)
                sum += h.from <= clock ? h.bytes : 0;
            return sum;
        }
        uint32_t count(bool Held::*which) const
        {
            uint32_t n = 0;
            for (const Held &h : held)
                n += h.from <= clock && h.*which ? 1 : 0;
            return n;
        }
        uint32_t largest() const
        {
            const uint32_t used = inUse();
            return used >= REST_LARGEST ? 0 : REST_LARGEST - used; // pessimistic: all in the largest block
        }
        void hold(uint32_t now, uint32_t bytes, uint32_t ms, bool inbound = false, bool reply = false)
        {
            held.push_back({now, now + ms, bytes, inbound, reply});
        }

        bool accept(uint32_t now)
        {
            const uint32_t live = count(&Held::inbound);
            const bool ok = plan.spec048 ? Budget::inbound(live, largest(), net.phase(now)) == Budget::Inbound::Accept
                                         : SQM::HeapSafe::acceptFits(largest());
            if (!ok)
            {
                ++refused;
                hold(now, REFUSED_BYTES, REFUSED_MS);
            }
            return ok;
        }

        void serve(uint32_t now, Client &c)
        {
            if (!accept(now))
            {
                c.dueMs = now + c.retryMs;
                return;
            }
            c.dueMs = c.everyMs != 0 ? now + c.everyMs : UINT32_MAX;
            if (c.kind == Client::Alpaca)
                hold(now, ALPACA_BYTES, ALPACA_MS, true);
            else if (c.kind == Client::WebSocket)
                hold(now, WS_HANDSHAKE_BYTES, WS_HANDSHAKE_MS, true);
            else
            {
                const bool window = plan.spec048 && net.inWindow(now);
                const uint32_t limit = Budget::repliesAllowed(window, SQM::HeapSafe::MAX_REPLIES_IN_FLIGHT);
                if (SQM::HeapSafe::replySlotFree("/api/status", false, count(&Held::reply), limit))
                    hold(now, STATUS_BYTES, STATUS_MS, true, true);
                else
                    hold(now, BUSY_BYTES, BUSY_MS, true);
            }
        }

        void polls(uint32_t now)
        {
            // The safety poller: weather then N.I.N.A., one after the other,
            // every 5 s from the return (spec 047 reads N.I.N.A. first; the
            // same bytes either way). Unchanged by spec 048.
            if (now < nextWeather)
                return;
            hold(now, WEATHER_BYTES, WEATHER_MS);
            held.push_back({now + WEATHER_MS, now + WEATHER_MS + PARK_MS, PARK_BYTES, false, false});
            nextWeather = now + 5000;
        }

        void mqttWork(uint32_t now)
        {
            const uint32_t since = net.sinceReturn(now);
            if (!mqttUp)
            {
                const bool due =
                    plan.spec048 ? mqtt.due(now, true, since) && Budget::backgroundFits(largest(), net.following(now)) : since >= 3000;
                if (!due)
                    return;
                hold(now, MQTT_CONNECT_BYTES, MQTT_CONNECT_MS);
                mqtt.finished(now, true);
                mqttUp = true;
                mqttAt = since;
                if (!plan.spec048)
                {
                    hold(now, DISCOVERY_BURST_BYTES, DISCOVERY_BURST_MS);
                    discoveryLast = since;
                }
                else
                    pacer.start(1, now, false); // the worst case: the broker kept nothing
                return;
            }
            if (!plan.spec048)
                return;
            if (!Budget::backgroundFits(largest(), net.following(now)))
                return;
            const SQM::HaDiscovery::Pacer::Step s = pacer.next(now, SQM::HaDiscovery::count());
            if (s.kind == SQM::HaDiscovery::Pacer::Kind::Entity || s.kind == SQM::HaDiscovery::Pacer::Kind::Marker)
            {
                hold(now, DISCOVERY_ONE_BYTES, DISCOVERY_ONE_MS);
                discoveryLast = since;
            }
            pacer.done(s, now);
        }

        void alerts(uint32_t now)
        {
            const uint32_t since = net.sinceReturn(now);
            const bool go = plan.spec048 ? Budget::alertsMayGo(true, since) : since >= 8000;
            if (!go || alertsLeft == 0 || now < nextAlert || !SQM::HeapSafe::alertFits(largest(), REST_FREE - inUse()))
                return;
            hold(now, ALERT_BYTES, ALERT_MS);
            --alertsLeft;
            ++alertsSent;
            nextAlert = now + (plan.spec048 ? Budget::alertGapMs(true, since) : 1000);
        }

        void step(uint32_t now)
        {
            clock = now;
            for (size_t i = 0; i < held.size();)
                if (held[i].until <= now)
                    held.erase(held.begin() + static_cast<long>(i));
                else
                    ++i;
            if (nextWeather == 0)
                nextWeather = now;
            polls(now);
            for (Client &c : clients)
                if (c.dueMs != UINT32_MAX && now >= c.dueMs)
                    serve(now, c);
            mqttWork(now);
            alerts(now);
            const uint32_t used = inUse();
            if (used > peak)
                peak = used;
        }
    };
} // namespace

namespace
{
    uint32_t before()
    {
        Model model(Plan{false});
        return model.run();
    }
} // namespace

void test_the_old_schedule_reproduces_the_collapse()
{
    Model before(Plan{false});
    const uint32_t peak = before.run();
    printf("spec 038 schedule: peak %u B held at once, %u refused\n", static_cast<unsigned>(peak), static_cast<unsigned>(before.refused));
    // F51: about 60 KB free at rest, 2.5 KB at the lowest. The model is
    // coarser than the board (no WiFi buffers, no retransmissions) but shows
    // the same pile-up in the first seconds.
    TEST_ASSERT_TRUE(peak >= 45000);
}

void test_the_new_schedule_keeps_the_return_bounded()
{
    Model after(Plan{true});
    const uint32_t peak = after.run();
    printf(
        "spec 048 schedule: peak %u B held at once, %u refused, MQTT at %u ms, discovery done at %u ms, %u alerts\n",
        static_cast<unsigned>(peak),
        static_cast<unsigned>(after.refused),
        static_cast<unsigned>(after.mqttAt),
        static_cast<unsigned>(after.discoveryLast),
        static_cast<unsigned>(after.alertsSent));
    // At most about half the old pile-up, and under half the free heap at
    // rest; the soak's floor (largest block 16 KB) is checked on the board.
    TEST_ASSERT_TRUE(peak <= 30000);
    TEST_ASSERT_TRUE(after.refused > 0); // clients were turned away, and came back
    TEST_ASSERT_TRUE(peak * 100 <= before() * 70);
    TEST_ASSERT_TRUE(after.mqttAt >= Budget::MQTT_RETURN_DELAY_MS);
    TEST_ASSERT_EQUAL_UINT32(QUEUED_ALERTS, after.alertsSent); // all sent within the 90 s
    TEST_ASSERT_TRUE(after.discoveryLast < 30000);             // discovery done within 30 s
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_old_schedule_reproduces_the_collapse);
    RUN_TEST(test_the_new_schedule_keeps_the_return_bounded);
    return UNITY_END();
}
