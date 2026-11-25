#pragma once

#include <cstddef>
#include <cstdint>

// How the device's TCP connections are shared (SQMeter spec 011 FR-008, 013
// FR-012). lwIP allows CONFIG_LWIP_MAX_ACTIVE_TCP (16) at once; every HTTP
// request uses a fresh one (the server sends Connection: close), so an imaging
// app polling the device needs free ones all the time. Live-update sockets are
// capped so browsers can never take them all.

namespace SQM
{
    namespace ConnectionBudget
    {
        constexpr size_t TCP_CONNECTIONS = 16; // CONFIG_LWIP_MAX_ACTIVE_TCP

        // WebSocket clients per endpoint (/ws/status, /ws/diag). Spec 037: a new one past the
        // limit is refused (closed with TOO_MANY_PAGES); the pages already open keep theirs.
        // Replacing the oldest instead made two pages over the limit take turns knocking each
        // other off, a reconnect every second or two. A tab that stopped reading is still
        // closed (stalledTooLong), and one that vanished misses its pings.
        constexpr size_t WEBSOCKETS_PER_ENDPOINT = 3;
        // The close code a refused client gets (4000-4999 are the application's; 429 as in HTTP).
        constexpr uint16_t TOO_MANY_PAGES = 4429;
        constexpr size_t WEBSOCKET_ENDPOINTS = 2;

        // Outbound: MQTT, plus one TLS connection at a time (alerts share one
        // lock).
        constexpr size_t OUTBOUND = 2;

        // What's always left for HTTP: N.I.N.A., ConformU, the web UI's pages.
        constexpr size_t httpReserve()
        {
            return TCP_CONNECTIONS - WEBSOCKETS_PER_ENDPOINT * WEBSOCKET_ENDPOINTS - OUTBOUND;
        }

        // An imaging app polling two devices, plus a browser loading a page.
        constexpr size_t MIN_HTTP_RESERVE = 6;

        static_assert(httpReserve() >= MIN_HTTP_RESERVE, "live-update sockets would crowd out imaging apps");

        // A live-update client is sent a ping this often; one that stops
        // acknowledging is dropped (AsyncTCP's 5 s acknowledgement timeout).
        constexpr unsigned WEBSOCKET_PING_SECONDS = 15;

        // A client whose send queue has stayed full this long has stopped
        // reading (a tab in a sleeping browser) and is closed; a brief
        // backlog on weak WiFi is not.
        constexpr unsigned long STALL_CLOSE_MS = 10000;

        // fullSinceMs: when its queue was first seen full (0 = not full).
        constexpr bool stalledTooLong(unsigned long fullSinceMs, unsigned long nowMs)
        {
            return fullSinceMs != 0 && nowMs - fullSinceMs >= STALL_CLOSE_MS;
        }

        // Live updates (spec 028): both streams (/ws/status, /ws/diag) send on
        // connect and then at one rate: every 2 s at rest, every 500 ms while the
        // roof is opening or closing. Spec 037: when memory is too short for a
        // whole document, a HEARTBEAT goes instead, so pages don't take the gap
        // for a dead connection and reconnect (which costs more memory). A status document is about 4 KB, so three
        // clients at this rate stay near 24 KB/s during a move.
        constexpr uint32_t LIVE_REST_MS = 2000;
        constexpr uint32_t LIVE_MOVING_MS = 500;

        // How long after the last live update the next one is due.
        constexpr uint32_t liveIntervalMs(bool roofMoving)
        {
            return roofMoving ? LIVE_MOVING_MS : LIVE_REST_MS;
        }

        // What a page is sent in place of a document it couldn't be given.
        constexpr const char *HEARTBEAT = "{\"heartbeat\":true}";

        // Should a new client be refused? `clientsOnEndpoint` counts it too.
        constexpr bool overLimit(size_t clientsOnEndpoint)
        {
            return clientsOnEndpoint > WEBSOCKETS_PER_ENDPOINT;
        }
    } // namespace ConnectionBudget
} // namespace SQM
