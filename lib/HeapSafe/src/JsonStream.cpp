#include "JsonStream.h"

namespace SQM
{
    namespace HeapSafe
    {
        // Passes an object's text on without its outer braces: drops the first
        // byte and holds each byte back by one, so the last ('}') never goes
        // out. The comma before the first member is written only when there is
        // a member ("{}" writes nothing).
        class InnerWriter
        {
        public:
            explicit InnerWriter(JsonStream &stream)
                : stream(stream)
            {
            }

            size_t write(uint8_t c)
            {
                if (seen++ == 0)
                    return 1;
                if (held)
                {
                    if (!started)
                    {
                        stream.separate();
                        started = true;
                    }
                    stream.out.write(last);
                }
                last = c;
                held = true;
                return 1;
            }

            size_t write(const uint8_t *data, size_t length)
            {
                for (size_t i = 0; i < length; ++i)
                    write(data[i]);
                return length;
            }

        private:
            JsonStream &stream;
            size_t seen = 0;
            uint8_t last = 0;
            bool held = false;
            bool started = false;
        };

        void JsonStream::separate()
        {
            const uint8_t bit = static_cast<uint8_t>(1U << (depth % MAX_DEPTH));
            if ((filled & bit) != 0)
                out += ',';
            filled |= bit;
        }

        void JsonStream::open(const char *key, char bracket)
        {
            if (depth > 0)
                separate();
            if (key != nullptr)
            {
                out += '"';
                out += key;
                out += "\":";
            }
            out += bracket;
            ++depth;
            if (depth >= MAX_DEPTH)
                out.fail(); // deeper than any reply here: refuse rather than guess
            filled &= static_cast<uint8_t>(~(1U << (depth % MAX_DEPTH)));
        }

        void JsonStream::close(char bracket)
        {
            if (depth > 0)
                --depth;
            out += bracket;
        }

        void JsonStream::beginObject(const char *key)
        {
            open(key, '{');
        }

        void JsonStream::endObject()
        {
            close('}');
        }

        void JsonStream::beginArray(const char *key)
        {
            open(key, '[');
        }

        void JsonStream::endArray()
        {
            close(']');
        }

        // A document whose pool couldn't be allocated makes the whole reply
        // fail (it would otherwise be missing silently); one that overflowed is
        // still written and flagged.
        bool JsonStream::usable(const JsonDocument &doc)
        {
            if (doc.capacity() == 0)
            {
                out.fail();
                return false;
            }
            anyOverflowed = anyOverflowed || doc.overflowed();
            return true;
        }

        void JsonStream::members(const JsonDocument &doc)
        {
            if (!usable(doc) || !doc.is<JsonObjectConst>())
                return;
            InnerWriter inner(*this);
            serializeJson(doc, inner);
        }

        void JsonStream::element(const JsonDocument &doc, const char *key)
        {
            if (!usable(doc))
                return;
            separate();
            if (key != nullptr)
            {
                out += '"';
                out += key;
                out += "\":";
            }
            serializeJson(doc, out);
        }
    } // namespace HeapSafe
} // namespace SQM
