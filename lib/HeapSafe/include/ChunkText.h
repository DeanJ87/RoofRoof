#pragma once

// Spec 031: a text held in small pieces, each from malloc and checked.
//
// The firmware has no C++ exceptions, so a std::string or std::vector that
// can't grow calls abort(). A reply written here never asks for more than one
// piece at a time and never copies what it already holds: when a piece can't
// be had the text is marked failed and the caller answers "busy" instead.
// ArduinoJson writes into it directly (serializeJson(doc, text)).

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace SQM
{
    namespace HeapSafe
    {
        constexpr size_t PIECE_BYTES = 1024;
        constexpr size_t MAX_PIECES = 24; // 24 KB: the largest reply is about 4.3 KB (research R8)

        using Allocate = void *(*)(size_t);

        class ChunkText
        {
        public:
            explicit ChunkText(size_t maxPieces = MAX_PIECES, Allocate allocate = std::malloc);
            ~ChunkText();
            ChunkText(const ChunkText &) = delete;
            ChunkText &operator=(const ChunkText &) = delete;
            ChunkText(ChunkText &&other) noexcept;
            ChunkText &operator=(ChunkText &&other) noexcept;

            // ArduinoJson's writer interface: the bytes written, 0 once failed.
            size_t write(uint8_t c);
            size_t write(const uint8_t *data, size_t length);

            ChunkText &append(const char *text, size_t length);
            ChunkText &append(const char *text);
            ChunkText &operator+=(const char *text) { return append(text); }
            ChunkText &operator+=(const std::string &text) { return append(text.data(), text.size()); }
            ChunkText &operator+=(char c);

            // False once a piece couldn't be allocated, the cap was reached or
            // fail() was called; what was written is kept but incomplete.
            bool ok() const { return !failed_; }
            void fail() { failed_ = true; }
            size_t size() const { return length_; }

            // Copies up to `max` bytes from `offset`; the bytes copied.
            size_t read(size_t offset, uint8_t *out, size_t max) const;
            // Frees every piece and starts again.
            void clear();
            // A std::string copy (tests, short logs).
            std::string str() const;

        private:
            bool reserveTail();
            void release();

            char *pieces_[MAX_PIECES] = {};
            size_t maxPieces_;
            Allocate allocate_;
            size_t length_ = 0;
            bool failed_ = false;
        };
    } // namespace HeapSafe
} // namespace SQM
