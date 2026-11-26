#pragma once

// Spec 031: every reply the firmware builds goes out through here.
//
// The firmware has no C++ exceptions: a std::string, std::vector or `new`
// that can't get memory calls abort(). A reply is written into a
// HeapSafe::ChunkText (1 KB pieces from malloc, each checked) and sent from
// those pieces; when memory runs out the client gets 503 "busy, try again"
// and the device keeps running. tools/firmware/test_heap_audit.py keeps
// replies from being written into growing strings again.

#include "ChunkText.h"

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebSocket;
class AsyncWebSocketClient;

namespace SQM
{
    namespace WebReply
    {
        // Sends `text` (moved into the response, freed once sent). 503 when the
        // text is incomplete or the response can't be allocated.
        void sendText(AsyncWebServerRequest *request, int code, const char *type, HeapSafe::ChunkText &&text);
        // Serializes `doc` into pieces and sends it; 503 when its pool or a
        // piece couldn't be allocated.
        void sendJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc);
        // Fix 041: a short reply (a roof command's outcome) sent from a buffer
        // on the stack, as a plain response: no pieces and no 2.9 KB send
        // buffer from the heap, so it goes out while the heap is short or the
        // reply cap is held. Longer than SMALL_JSON_BYTES: sendJson.
        constexpr size_t SMALL_JSON_BYTES = 768;
        void sendSmallJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc);
        // The shared 503 (contracts/busy-reply.md), counted.
        void sendBusy(AsyncWebServerRequest *request);

        // A WebSocket frame from pieces, copied into one of a few buffers kept
        // for the device's lifetime (spec 037); skipped and counted while each
        // is still being sent. True when queued.
        bool textAll(AsyncWebSocket &socket, const HeapSafe::ChunkText &text);
        bool text(AsyncWebSocketClient *client, const HeapSafe::ChunkText &text);
        // Spec 037: the short ConnectionBudget::HEARTBEAT in place of a frame
        // that couldn't be sent (a few bytes from the heap).
        void heartbeatAll(AsyncWebSocket &socket);
        void heartbeat(AsyncWebSocketClient *client);
        // The frame buffers, at start while memory is plentiful (spec 037).
        void reserveFrames();

        // The memory guard (FR-008): refuses non-essential requests before
        // their handler runs while the heap is short.
        void installGuard(AsyncWebServer &server);

        // A short error reply ({"error": "..."}) on the stack: no heap (the
        // message is cut to fit).
        struct ErrorText
        {
            char text[384];
            const char *c_str() const { return text; }
        };
        ErrorText errorJson(const char *message);

        // Since start (FR-010).
        uint32_t refused();
        uint32_t skippedFrames();
    } // namespace WebReply
} // namespace SQM
