#pragma once

// Spec 048: what happens when the network comes back.
//
// After an outage everything that talks over the network starts again in the
// same seconds: the clients that poll the device (N.I.N.A.'s Alpaca, a
// harness, pages reconnecting their live status), MQTT with its Home
// Assistant discovery, the queued alerts, the safety polls catching up. Each
// takes its own blocks; together they took the heap from about 60 KB free to
// 2.5 KB on the board (shakedown F51). This is the schedule that spreads them
// out: when a return happened, how long its window lasts, what may start in
// it, and a record of each return for GET /api/diag/heap. Pure logic: time
// and readings come in as parameters.

#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace ConnectionBudget
    {
        // A return's window: the inbound connections and built replies are
        // held to fewer at once (inbound(), repliesAllowed()).
        constexpr uint32_t RETURN_WINDOW_MS = 30000;
        // How long a return's record keeps collecting the heap's lows, and the
        // queued alerts go one at a time, slowly (alertGapMs()).
        constexpr uint32_t RETURN_FOLLOW_MS = 60000;
        // The link stayed up but no client connected for this long, then one
        // did: the traffic came back (an access point that lost its uplink
        // but kept its clients). Counted as a return too.
        constexpr uint32_t QUIET_RETURN_MS = 30000;

        // MQTT reconnects this long after a return (was 3 s, spec 038): after
        // the polls and the clients' first requests have been answered.
        constexpr uint32_t MQTT_RETURN_DELAY_MS = 10000;
        // Between connect attempts: 5 s, doubled after each failure up to 30 s.
        constexpr uint32_t MQTT_RETRY_MS = 5000;
        constexpr uint32_t MQTT_RETRY_MAX_MS = 30000;

        // Queued alerts start this long after a return (was 8 s, spec 038),
        // behind MQTT's connect, and go one every ALERT_RETURN_GAP_MS until
        // RETURN_FOLLOW_MS has passed; otherwise one a second.
        constexpr uint32_t ALERT_RETURN_DELAY_MS = 15000;
        constexpr uint32_t ALERT_RETURN_GAP_MS = 5000;
        constexpr uint32_t ALERT_GAP_MS = 1000;

        // Where the device is after a return: its window, then the rest of
        // the time it is followed, then settled.
        enum class ReturnPhase : uint8_t
        {
            Settled,
            Window,    // the first RETURN_WINDOW_MS
            Following, // until RETURN_FOLLOW_MS
        };

        // Inbound HTTP connections (requests not yet finished; a WebSocket
        // counts until its upgrade) at once. 8 is what the TCP budget leaves
        // HTTP (httpReserve()). In a return's window 3, then 4 until it is
        // settled; past the first only while the largest block is at least
        // RETURN_ACCEPT_BLOCK_BYTES (the soak's floor: a status reply's 12 KB
        // beside it would cut into it). The clients turned away retry; easing
        // the cap in two steps keeps their retries from all landing at once.
        constexpr size_t MAX_LIVE_INBOUND = 8;
        constexpr size_t RETURN_LIVE_INBOUND = 3;
        constexpr size_t FOLLOW_LIVE_INBOUND = 4;
        constexpr uint32_t RETURN_ACCEPT_BLOCK_BYTES = 16384;

        enum class Inbound : uint8_t
        {
            Accept,
            Busy, // answered 503 at once, before any request is read
        };

        // Whether a new inbound connection is taken, with `live` already open.
        // The first one is always taken (a person's Stop gets a slot as soon
        // as the one before it is answered).
        constexpr Inbound inbound(size_t live, uint32_t largestBlock, ReturnPhase phase)
        {
            if (live >= MAX_LIVE_INBOUND)
                return Inbound::Busy;
            if (phase == ReturnPhase::Settled || live == 0)
                return Inbound::Accept;
            const size_t cap = phase == ReturnPhase::Window ? RETURN_LIVE_INBOUND : FOLLOW_LIVE_INBOUND;
            if (live >= cap || largestBlock < RETURN_ACCEPT_BLOCK_BYTES)
                return Inbound::Busy;
            return Inbound::Accept;
        }

        // Work the device starts on its own (MQTT's connect, each discovery
        // config): while a return is followed only with the largest block at
        // RETURN_ACCEPT_BLOCK_BYTES, otherwise at least BACKGROUND_MIN_BLOCK_BYTES
        // (HeapSafe::MIN_BLOCK_BYTES, what a reply needs).
        constexpr uint32_t BACKGROUND_MIN_BLOCK_BYTES = 6144;
        constexpr bool backgroundFits(uint32_t largestBlock, bool following)
        {
            return largestBlock >= (following ? RETURN_ACCEPT_BLOCK_BYTES : BACKGROUND_MIN_BLOCK_BYTES);
        }

        // Built replies being sent at once (HeapSafe::MAX_REPLIES_IN_FLIGHT, 2);
        // one in a return's window.
        constexpr uint32_t RETURN_REPLIES_IN_FLIGHT = 1;

        // How many built replies may be sent at once now.
        constexpr uint32_t repliesAllowed(bool returnWindow, uint32_t usual)
        {
            return returnWindow && usual > RETURN_REPLIES_IN_FLIGHT ? RETURN_REPLIES_IN_FLIGHT : usual;
        }

        // Whether queued alerts may go now (`online`: the network is up;
        // `sinceReturnMs`: since it last came back).
        constexpr bool alertsMayGo(bool online, uint32_t sinceReturnMs)
        {
            return !online || sinceReturnMs >= ALERT_RETURN_DELAY_MS;
        }

        // How long after one alert step the next is due.
        constexpr uint32_t alertGapMs(bool online, uint32_t sinceReturnMs)
        {
            return online && sinceReturnMs < RETURN_FOLLOW_MS ? ALERT_RETURN_GAP_MS : ALERT_GAP_MS;
        }

        // When MQTT may try to connect: one attempt at a time (the caller runs
        // it to the end before asking again), MQTT_RETURN_DELAY_MS after a
        // return, and backing off while the broker doesn't answer.
        class MqttRetry
        {
        public:
            bool due(uint32_t nowMs, bool online, uint32_t sinceReturnMs) const;
            // An attempt ended at `nowMs`.
            void finished(uint32_t nowMs, bool connected);
            // The network came back: the next attempt waits only for the return's delay.
            void networkReturned() { failures = 0; }
            uint32_t intervalMs() const;

        private:
            bool tried = false;
            uint32_t lastEndMs = 0;
            uint8_t failures = 0;
        };

        enum class ReturnKind : uint8_t
        {
            FirstUp,      // the first time on a network since start
            LinkBack,     // the link (WiFi or Ethernet) came back
            RequestsBack, // the link stayed, clients came back after QUIET_RETURN_MS
        };

        enum class DiscoveryOutcome : uint8_t
        {
            Pending,
            Skipped,   // the broker still had it (spec 048 marker)
            Published, // published again, one entity at a time
        };

        // One return, as GET /api/diag/heap prints it.
        struct ReturnRecord
        {
            uint32_t atMs = 0;   // millis() when it came back
            uint32_t downMs = 0; // how long it was gone (0 for FirstUp)
            ReturnKind kind = ReturnKind::FirstUp;
            // The heap's lows over the RETURN_FOLLOW_MS after it.
            uint32_t lowestLargest = UINT32_MAX;
            uint32_t lowestFree = UINT32_MAX;
            uint32_t refused = 0;     // inbound connections answered busy at once
            uint16_t peakLive = 0;    // most inbound connections open at once
            uint32_t mqttAfterMs = 0; // MQTT connected this long after it (0: not yet)
            DiscoveryOutcome discovery = DiscoveryOutcome::Pending;
        };

        class NetworkReturn
        {
        public:
            static constexpr size_t KEPT = 8;

            // Main loop, every pass: whether the network is up and when the
            // last inbound connection arrived (0: none yet).
            void update(bool online, uint32_t nowMs, uint32_t lastInboundMs);

            bool online() const { return up; }
            // Since the last return (or the first time up); 0 while down.
            uint32_t sinceReturn(uint32_t nowMs) const { return up ? nowMs - returnAtMs : 0; }
            // Since the link itself came up (spec 038's settle); 0 while down.
            uint32_t sinceLinkUp(uint32_t nowMs) const { return up ? nowMs - linkUpMs : 0; }
            bool inWindow(uint32_t nowMs) const { return up && sinceReturn(nowMs) < RETURN_WINDOW_MS; }
            bool following(uint32_t nowMs) const { return count > 0 && up && sinceReturn(nowMs) < RETURN_FOLLOW_MS; }
            ReturnPhase phase(uint32_t nowMs) const
            {
                return inWindow(nowMs) ? ReturnPhase::Window : following(nowMs) ? ReturnPhase::Following : ReturnPhase::Settled;
            }
            // Returns since start (FirstUp not counted).
            uint32_t returns() const { return returnCount; }

            // What the newest return's record collects while following().
            void noteHeap(uint32_t nowMs, uint32_t largestBlock, uint32_t freeBytes);
            // `refusedTotal`: the gate's running count since start.
            void noteInbound(uint32_t nowMs, size_t live, uint32_t refusedTotal);
            void noteMqttConnected(uint32_t nowMs);
            void noteDiscovery(DiscoveryOutcome outcome);

            size_t size() const { return count; }
            // Newest first.
            const ReturnRecord &at(size_t newestFirst) const;

        private:
            void begin(uint32_t nowMs, uint32_t downMs, ReturnKind kind);

            ReturnRecord records[KEPT] = {};
            size_t newest = 0;
            size_t count = 0;
            uint32_t returnCount = 0;
            bool up = false;
            bool everUp = false;
            uint32_t downSinceMs = 0;
            uint32_t linkUpMs = 0;
            uint32_t returnAtMs = 0;
            uint32_t inboundSeenMs = 0; // the last inbound arrival seen
            uint32_t refusedAtReturn = 0;
            uint32_t refusedSeen = 0;
        };

        // One return as a line of GET /api/diag/heap (no newline): the length
        // written, at most `size` - 1.
        size_t describe(const ReturnRecord &record, uint32_t nowMs, char *out, size_t size);
        // Whether a kept return came back in minute `index` (millis() / 60 s,
        // HeapSafe::HeapTrend's minutes).
        bool returnedInMinute(const NetworkReturn &returns, uint32_t minuteIndex);
    } // namespace ConnectionBudget
} // namespace SQM
