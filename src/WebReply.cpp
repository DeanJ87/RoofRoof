#include "WebReply.h"

#include <Arduino.h>

#include "ConnectionBudget.h"
#include "FramePool.h"
#include "HeapReserve.h"
#include "HeapGuard.h"
#include "Logger.h"
#include "ReplyLedger.h"
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_heap_caps.h>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace SQM
{
    namespace WebReply
    {
        namespace
        {
            std::atomic<uint32_t> refusedCount{0};
            std::atomic<uint32_t> skippedCount{0};
            HeapSafe::ReplyLedger replies; // spec 037: ChunkTextResponses alive; fix 041: and since when

            constexpr const char *BUSY_BODY = "{\"error\":\"The device is busy: try again\"}";

            uint32_t largestBlock()
            {
                return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            }

            // Spec 038: while the emergency reserve is spent the heap counts as
            // short, so only essential requests run until the loop takes it back.
            uint32_t guardBlock()
            {
                return HeapSafe::reserve().held() ? largestBlock() : 0;
            }

            // The web server fills its own send buffer (2.9 KB) from the
            // pieces, a window at a time; nothing else is copied.
            class ChunkTextResponse : public AsyncAbstractResponse
            {
            public:
                ChunkTextResponse(int code, const char *type, HeapSafe::ChunkText &&text)
                    : text_(std::move(text))
                {
                    _code = code;
                    _contentType = type;
                    _contentLength = text_.size();
                    slot_ = replies.open(millis());
                }

                ~ChunkTextResponse() override { replies.close(slot_); }

                bool _sourceValid() const override { return true; }

                size_t _fillBuffer(uint8_t *buffer, size_t maxLen) override
                {
                    const size_t n = text_.read(offset_, buffer, maxLen);
                    offset_ += n;
                    return n;
                }

            private:
                HeapSafe::ChunkText text_;
                size_t offset_ = 0;
                size_t slot_ = HeapSafe::ReplyLedger::NO_SLOT;
            };

            // Fix 041: replies stuck in sending (their connection stopped
            // moving) no longer hold the cap; logged when their number changes.
            uint32_t repliesSending()
            {
                static std::atomic<uint32_t> staleSeen{0};
                const uint32_t now = millis();
                const uint32_t stale = replies.stale(now);
                if (staleSeen.exchange(stale) != stale && stale > 0)
                    Logger::warn(
                        "WebReply",
                        "%u replies still sending after %u s: no longer counted against the cap",
                        static_cast<unsigned>(stale),
                        static_cast<unsigned>(HeapSafe::ReplyLedger::STALE_MS / 1000));
                return replies.sending(now);
            }

            // Spec 037: frames come from buffers kept for the device's lifetime.
            HeapSafe::FramePool framePool;

            // The frame's one buffer: a pooled one; past the pool's size, a
            // fresh one only when the heap can spare it (spec 031 research R5).
            // None free (a client is still sending each): skipped.
            std::shared_ptr<std::vector<uint8_t>> frameOf(const HeapSafe::ChunkText &text)
            {
                if (!text.ok() || text.size() == 0)
                {
                    ++skippedCount;
                    return nullptr;
                }
                std::shared_ptr<std::vector<uint8_t>> frame = framePool.take(text.size());
                if (!frame && text.size() > HeapSafe::FramePool::CAPACITY && HeapSafe::frameFits(text.size(), largestBlock()))
                    frame = std::make_shared<std::vector<uint8_t>>(text.size()); // heap-audit: checked by frameFits above
                if (!frame)
                {
                    ++skippedCount;
                    return nullptr;
                }
                text.read(0, frame->data(), frame->size());
                return frame;
            }
        } // namespace

        void sendBusy(AsyncWebServerRequest *request)
        {
            ++refusedCount;
            AsyncWebServerResponse *response = request->beginResponse(503, "application/json", BUSY_BODY);
            response->addHeader("Retry-After", "2");
            request->send(response);
        }

        void sendText(AsyncWebServerRequest *request, int code, const char *type, HeapSafe::ChunkText &&text)
        {
            if (!text.ok())
            {
                sendBusy(request);
                return;
            }
            auto *response = new (std::nothrow) ChunkTextResponse(code, type, std::move(text));
            if (response == nullptr)
            {
                sendBusy(request);
                return;
            }
            request->send(response);
        }

        void sendJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc)
        {
            HeapSafe::ChunkText text;
            if (doc.capacity() == 0)
                text.fail(); // its pool couldn't be allocated: "null" is no reply
            else
                serializeJson(doc, text);
            sendText(request, code, "application/json", std::move(text));
        }

        void sendSmallJson(AsyncWebServerRequest *request, int code, const JsonDocument &doc)
        {
            char text[SMALL_JSON_BYTES];
            if (measureJson(doc) >= sizeof(text))
            {
                sendJson(request, code, doc);
                return;
            }
            serializeJson(doc, text, sizeof(text));
            request->send(code, "application/json", text);
        }

        bool textAll(AsyncWebSocket &socket, const HeapSafe::ChunkText &text)
        {
            auto frame = frameOf(text);
            return frame && socket.textAll(frame) != AsyncWebSocket::DISCARDED;
        }

        bool text(AsyncWebSocketClient *client, const HeapSafe::ChunkText &text)
        {
            auto frame = frameOf(text);
            return frame && client->text(frame);
        }

        Frame pooledFrame()
        {
            return framePool.take(HeapSafe::FramePool::CAPACITY);
        }

        bool frameAll(AsyncWebSocket &socket, Frame frame, size_t length)
        {
            if (!frame || length == 0 || length > frame->size())
            {
                ++skippedCount;
                return false;
            }
            frame->resize(length); // shorter: within its capacity, nothing allocated
            return socket.textAll(frame) != AsyncWebSocket::DISCARDED;
        }

        bool frameTo(AsyncWebSocketClient *client, Frame frame, size_t length)
        {
            if (!frame || length == 0 || length > frame->size())
            {
                ++skippedCount;
                return false;
            }
            frame->resize(length);
            return client->text(frame);
        }

        void noteSkipped()
        {
            ++skippedCount;
        }

        void heartbeatAll(AsyncWebSocket &socket)
        {
            socket.textAll(ConnectionBudget::HEARTBEAT);
        }

        void heartbeat(AsyncWebSocketClient *client)
        {
            client->text(ConnectionBudget::HEARTBEAT);
        }

        void reserveFrames()
        {
            framePool.reserve();
        }

        void installGuard(AsyncWebServer &server)
        {
            server.addMiddleware(
                [](AsyncWebServerRequest *request, ArMiddlewareNext next)
                {
                    const uint32_t freeHeap = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
                    if (HeapSafe::admit(request->url().c_str(), request->method() == HTTP_POST, guardBlock(), freeHeap) &&
                        HeapSafe::replySlotFree(request->url().c_str(), request->method() == HTTP_POST, repliesSending()))
                        next();
                    else
                        sendBusy(request);
                });
        }

        ErrorText errorJson(const char *message)
        {
            ErrorText out;
            StaticJsonDocument<64> doc;
            doc["error"] = message; // referenced, not copied
            if (measureJson(doc) < sizeof(out.text))
            {
                serializeJson(doc, out.text, sizeof(out.text));
                return out;
            }
            // Cut at a character boundary, leaving room for escapes and the key.
            char cut[160]; // escaped, at most twice as long: still fits out.text
            size_t n = std::min(sizeof(cut) - 4, strlen(message));
            while (n > 0 && (static_cast<uint8_t>(message[n]) & 0xC0) == 0x80)
                --n;
            memcpy(cut, message, n);
            memcpy(cut + n, "...", 4);
            doc["error"] = static_cast<const char *>(cut);
            serializeJson(doc, out.text, sizeof(out.text));
            return out;
        }

        uint32_t refused()
        {
            return refusedCount.load();
        }

        uint32_t skippedFrames()
        {
            return skippedCount.load();
        }
    } // namespace WebReply
} // namespace SQM
