#include "WebServer.h"
#include "ConnectionBudget.h"
#include "Logger.h"

#include <cstdint>
#include <vector>

// Live updates: /ws/status, kept within the connection budget (SQMeter spec
// 011 FR-008). RoofRoof spec 008 adds the roof's live stream.

namespace SQM
{
    void WebServer::setupWebSocket()
    {
        WebReply::reserveFrames();
        wsStatus.onEvent([this](AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *, uint8_t *, size_t)
                         { onStatusWebSocketEvent(client, type); });
        server.addHandler(&wsStatus);
    }

    bool WebServer::prepareWebSocketClient(AsyncWebSocket &socket, std::map<uint32_t, uint32_t> &clients, AsyncWebSocketClient *client)
    {
        // Ping quiet clients so a peer that vanished is noticed (AsyncTCP drops
        // one that doesn't acknowledge); SQMeter spec 011 FR-008.
        client->keepAlivePeriod(ConnectionBudget::WEBSOCKET_PING_SECONDS);
        // Spec 037: one past the limit is refused, from the loop task
        // (capWebSocketClients): closing it here, inside the library's connect
        // callback, could free it while the library still holds it.
        const bool refused = ConnectionBudget::overLimit(socket.count());
        const std::lock_guard<std::mutex> lock(wsClientsLock);
        clients[client->id()] = refused ? WS_REFUSE : 0;
        return !refused;
    }

    void WebServer::noteWebSocketClosed(std::map<uint32_t, uint32_t> &clients, AsyncWebSocketClient *client)
    {
        const std::lock_guard<std::mutex> lock(wsClientsLock);
        clients.erase(client->id());
    }

    void WebServer::capWebSocketClients(AsyncWebSocket &socket, std::map<uint32_t, uint32_t> &clients)
    {
        std::vector<uint32_t> refuse;
        {
            const std::lock_guard<std::mutex> lock(wsClientsLock);
            for (auto &entry : clients)
                if (entry.second == WS_REFUSE)
                {
                    refuse.push_back(entry.first);
                    entry.second = WS_REFUSED;
                }
        }
        // By id, under the socket's own lock (never ours: see closeStalledClients).
        for (uint32_t id : refuse)
        {
            ++wsRefused;
            socket.close(id, ConnectionBudget::TOO_MANY_PAGES, "too many open pages");
        }
        // Only frees clients that have gone; never closes one (the limit is kept above).
        socket.cleanupClients(UINT16_MAX);
    }

    void WebServer::closeStalledClients(AsyncWebSocket &socket, std::map<uint32_t, uint32_t> &clients, uint32_t now)
    {
        // A tab in a sleeping browser keeps its socket open but stops reading:
        // its queue stays full. Close it so it doesn't hold a connection.
        // The library calls our connect handler while holding its own lock, so
        // never hold wsClientsLock while calling into the socket: copy, check,
        // write back.
        // Spec 045: copied onto the stack (a map copy was a few heap nodes
        // twice a second); more than fit are checked on a later pass.
        constexpr size_t SNAPSHOT_MAX = 8;
        std::pair<uint32_t, uint32_t> snapshot[SNAPSHOT_MAX];
        size_t count = 0;
        {
            const std::lock_guard<std::mutex> lock(wsClientsLock);
            for (const auto &entry : clients)
            {
                if (count == SNAPSHOT_MAX)
                    break;
                snapshot[count++] = entry;
            }
        }
        for (size_t i = 0; i < count; ++i)
        {
            auto &entry = snapshot[i];
            if (entry.second >= WS_REFUSED)
                continue; // being refused (capWebSocketClients)
            // By id, under the socket's own lock: the network task can drop a
            // client at any moment.
            if (socket.availableForWrite(entry.first))
                entry.second = 0;
            else if (entry.second == 0)
                entry.second = now == 0 ? 1 : now;
            else if (ConnectionBudget::stalledTooLong(entry.second, now))
            {
                ++wsStalled;
                entry.second = 0;
                socket.close(entry.first);
            }
        }
        const std::lock_guard<std::mutex> lock(wsClientsLock);
        for (size_t i = 0; i < count; ++i)
        {
            auto found = clients.find(snapshot[i].first);
            if (found != clients.end())
                found->second = snapshot[i].second;
        }
    }

    void WebServer::broadcastStatusData()
    {
        // A client that stops reading is closed after a while
        // (closeStalledClients); a full queue just skips it meanwhile, so one
        // stuck tab never holds up the rest.
        closeStalledClients(wsStatus, wsStatusClients, millis());
        if (wsStatus.count() == 0)
            return;

        // Spec 045: written straight into a pooled frame, nothing from the heap.
        // Every one still being sent: skipped, as frameOf() would have after
        // writing the whole document into pieces.
        WebReply::Frame frame = WebReply::pooledFrame();
        if (!frame)
        {
            WebReply::noteSkipped();
            WebReply::heartbeatAll(wsStatus);
            return;
        }
        {
            HeapSafe::ChunkText text = HeapSafe::ChunkText::over(frame->data(), frame->size());
            writeStatus(text);
            if (text.ok())
            {
                if (!WebReply::frameAll(wsStatus, std::move(frame), text.size()))
                    WebReply::heartbeatAll(wsStatus);
                return;
            }
            // Longer than a pooled frame: from pieces, as before.
        }
        frame.reset();
        HeapSafe::ChunkText text;
        writeStatus(text);
        if (!WebReply::textAll(wsStatus, text))
            WebReply::heartbeatAll(wsStatus); // spec 037: short of memory, still alive
    }

    void WebServer::sendFirstStatus(AsyncWebSocketClient *client) const
    {
        // Spec 045: into a pooled frame when one is free (the network task:
        // its own section pool, not the loop's).
        if (WebReply::Frame frame = WebReply::pooledFrame())
        {
            HeapSafe::ChunkText text = HeapSafe::ChunkText::over(frame->data(), frame->size());
            writeStatus(text);
            if (text.ok())
            {
                if (!WebReply::frameTo(client, std::move(frame), text.size()))
                    WebReply::heartbeat(client);
                return;
            }
        }
        HeapSafe::ChunkText text;
        writeStatus(text);
        if (!WebReply::text(client, text))
            WebReply::heartbeat(client);
    }

    void WebServer::onStatusWebSocketEvent(AsyncWebSocketClient *client, AwsEventType type)
    {
        switch (type)
        {
        case WS_EVT_CONNECT:
            if (!prepareWebSocketClient(wsStatus, wsStatusClients, client))
            {
                Logger::warn(TAG, "Status WebSocket client %u refused: too many open pages", client->id());
                break;
            }
            Logger::info(TAG, "Status WebSocket client connected: %u", client->id());
            // The first frame; a heartbeat while memory is short (the next broadcast follows).
            sendFirstStatus(client);
            break;

        case WS_EVT_DISCONNECT:
            noteWebSocketClosed(wsStatusClients, client);
            Logger::info(TAG, "Status WebSocket client disconnected: %u", client->id());
            break;

        case WS_EVT_ERROR:
            Logger::error(TAG, "WebSocket error: %u", client->id());
            break;

        case WS_EVT_DATA:
            // Handle incoming WebSocket messages if needed
            break;

        default:
            break;
        }
    }

} // namespace SQM
