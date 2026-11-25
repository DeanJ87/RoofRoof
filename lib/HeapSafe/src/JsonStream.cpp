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
            explicit InnerWriter(JsonStream &stream) : stream_(stream) {}

            size_t write(uint8_t c)
            {
                if (seen_++ == 0)
                    return 1;
                if (held_)
                {
                    if (!started_)
                    {
                        stream_.separate();
                        started_ = true;
                    }
                    stream_.out_.write(last_);
                }
                last_ = c;
                held_ = true;
                return 1;
            }

            size_t write(const uint8_t *data, size_t length)
            {
                for (size_t i = 0; i < length; ++i)
                    write(data[i]);
                return length;
            }

        private:
            JsonStream &stream_;
            size_t seen_ = 0;
            uint8_t last_ = 0;
            bool held_ = false;
            bool started_ = false;
        };

        void JsonStream::separate()
        {
            const uint8_t bit = static_cast<uint8_t>(1U << (depth_ % MAX_DEPTH));
            if ((filled_ & bit) != 0)
                out_ += ',';
            filled_ |= bit;
        }

        void JsonStream::open(const char *key, char bracket)
        {
            if (depth_ > 0)
                separate();
            if (key != nullptr)
            {
                out_ += '"';
                out_ += key;
                out_ += "\":";
            }
            out_ += bracket;
            ++depth_;
            if (depth_ >= MAX_DEPTH)
                out_.fail(); // deeper than any reply here: refuse rather than guess
            filled_ &= static_cast<uint8_t>(~(1U << (depth_ % MAX_DEPTH)));
        }

        void JsonStream::close(char bracket)
        {
            if (depth_ > 0)
                --depth_;
            out_ += bracket;
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
                out_.fail();
                return false;
            }
            overflowed_ = overflowed_ || doc.overflowed();
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
                out_ += '"';
                out_ += key;
                out_ += "\":";
            }
            serializeJson(doc, out_);
        }
    } // namespace HeapSafe
} // namespace SQM
