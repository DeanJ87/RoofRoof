#pragma once

// Spec 031: one JSON object or array written into a ChunkText from sections,
// each section a small document serialized and joined to the rest, so a large
// reply needs one small pool (reused) instead of one pool for the whole reply.
//
//   JsonStream json(text);
//   json.beginObject();
//   doc.clear(); writeFirmware(doc); json.members(doc);   // {"firmware":{...}, ...
//   json.beginArray("alerts");
//   for (...) { item.clear(); ...; json.element(item); }
//   json.endArray();
//   json.endObject();
//
// Keys given here are written as they are: plain ASCII names from the code.

#include "ChunkText.h"

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

namespace SQM
{
    namespace HeapSafe
    {
        class JsonStream
        {
        public:
            explicit JsonStream(ChunkText &out)
                : out(out)
            {
            }

            // `key` when inside an object, nullptr at the top or in an array.
            void beginObject(const char *key = nullptr);
            void endObject();
            void beginArray(const char *key = nullptr);
            void endArray();
            // The members of an object document, at the current object's level.
            void members(const JsonDocument &doc);
            // A document as one value: an array element, or `key`'s value in an object.
            void element(const JsonDocument &doc, const char *key = nullptr);

            // A document written so far had overflowed its pool (members missing).
            bool overflowed() const { return anyOverflowed; }

        private:
            static constexpr uint8_t MAX_DEPTH = 8;
            void separate();
            void open(const char *key, char bracket);
            void close(char bracket);
            bool usable(const JsonDocument &doc);

            ChunkText &out;
            uint8_t depth = 0;
            uint8_t filled = 0; // bit n: level n already holds a member or element
            bool anyOverflowed = false;
            friend class InnerWriter;
        };
    } // namespace HeapSafe
} // namespace SQM
