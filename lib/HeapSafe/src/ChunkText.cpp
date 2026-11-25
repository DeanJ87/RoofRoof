#include "ChunkText.h"

#include <algorithm>
#include <cstring>

namespace SQM
{
    namespace HeapSafe
    {
        ChunkText::ChunkText(size_t maxPieces, Allocate allocate)
            : maxPieces_(std::min(maxPieces, MAX_PIECES)), allocate_(allocate)
        {
        }

        ChunkText::~ChunkText()
        {
            release();
        }

        ChunkText::ChunkText(ChunkText &&other) noexcept
            : maxPieces_(other.maxPieces_), allocate_(other.allocate_), length_(other.length_), failed_(other.failed_)
        {
            std::memcpy(pieces_, other.pieces_, sizeof(pieces_));
            std::memset(other.pieces_, 0, sizeof(other.pieces_));
            other.length_ = 0;
        }

        ChunkText &ChunkText::operator=(ChunkText &&other) noexcept
        {
            if (this != &other)
            {
                release();
                std::memcpy(pieces_, other.pieces_, sizeof(pieces_));
                std::memset(other.pieces_, 0, sizeof(other.pieces_));
                maxPieces_ = other.maxPieces_;
                allocate_ = other.allocate_;
                length_ = other.length_;
                failed_ = other.failed_;
                other.length_ = 0;
            }
            return *this;
        }

        void ChunkText::release()
        {
            for (char *&piece : pieces_)
            {
                std::free(piece);
                piece = nullptr;
            }
        }

        // The piece the next byte goes into exists (allocated now if needed).
        bool ChunkText::reserveTail()
        {
            if (failed_)
                return false;
            const size_t index = length_ / PIECE_BYTES;
            if (index >= maxPieces_)
            {
                failed_ = true;
                return false;
            }
            if (pieces_[index] == nullptr)
            {
                pieces_[index] = static_cast<char *>(allocate_(PIECE_BYTES));
                failed_ = pieces_[index] == nullptr;
            }
            return !failed_;
        }

        size_t ChunkText::write(uint8_t c)
        {
            if (!reserveTail())
                return 0;
            pieces_[length_ / PIECE_BYTES][length_ % PIECE_BYTES] = static_cast<char>(c);
            ++length_;
            return 1;
        }

        size_t ChunkText::write(const uint8_t *data, size_t length)
        {
            size_t written = 0;
            while (written < length && reserveTail())
            {
                const size_t offset = length_ % PIECE_BYTES;
                const size_t n = std::min(length - written, PIECE_BYTES - offset);
                std::memcpy(pieces_[length_ / PIECE_BYTES] + offset, data + written, n);
                length_ += n;
                written += n;
            }
            return written;
        }

        ChunkText &ChunkText::append(const char *text, size_t length)
        {
            write(reinterpret_cast<const uint8_t *>(text), length);
            return *this;
        }

        ChunkText &ChunkText::append(const char *text)
        {
            return text == nullptr ? *this : append(text, std::strlen(text));
        }

        ChunkText &ChunkText::operator+=(char c)
        {
            write(static_cast<uint8_t>(c));
            return *this;
        }

        size_t ChunkText::read(size_t offset, uint8_t *out, size_t max) const
        {
            size_t copied = 0;
            while (copied < max && offset < length_)
            {
                const size_t within = offset % PIECE_BYTES;
                const size_t n = std::min({max - copied, PIECE_BYTES - within, length_ - offset});
                std::memcpy(out + copied, pieces_[offset / PIECE_BYTES] + within, n);
                copied += n;
                offset += n;
            }
            return copied;
        }

        void ChunkText::clear()
        {
            release();
            length_ = 0;
            failed_ = false;
        }

        std::string ChunkText::str() const
        {
            std::string out;
            for (size_t offset = 0; offset < length_; offset += PIECE_BYTES)
                out.append(pieces_[offset / PIECE_BYTES], std::min(PIECE_BYTES, length_ - offset));
            return out;
        }
    } // namespace HeapSafe
} // namespace SQM
