#include "ChunkText.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace SQM
{
    namespace HeapSafe
    {
        ChunkText::ChunkText(size_t maxPieces, Allocate allocate)
            : maxPieces(std::min(maxPieces, MAX_PIECES)),
              allocator(allocate)
        {
        }

        ChunkText::~ChunkText()
        {
            release();
        }

        ChunkText::ChunkText(ChunkText &&other) noexcept
            : maxPieces(other.maxPieces),
              allocator(other.allocator),
              length(other.length),
              failed(other.failed)
        {
            std::copy(std::begin(other.pieces), std::end(other.pieces), std::begin(pieces));
            std::fill(std::begin(other.pieces), std::end(other.pieces), nullptr);
            other.length = 0;
        }

        ChunkText &ChunkText::operator=(ChunkText &&other) noexcept
        {
            if (this != &other)
            {
                release();
                std::copy(std::begin(other.pieces), std::end(other.pieces), std::begin(pieces));
                std::fill(std::begin(other.pieces), std::end(other.pieces), nullptr);
                maxPieces = other.maxPieces;
                allocator = other.allocator;
                length = other.length;
                failed = other.failed;
                other.length = 0;
            }
            return *this;
        }

        void ChunkText::release()
        {
            for (char *&piece : pieces)
            {
                std::free(piece);
                piece = nullptr;
            }
        }

        // The piece the next byte goes into exists (allocated now if needed).
        bool ChunkText::reserveTail()
        {
            if (failed)
                return false;
            const size_t index = length / PIECE_BYTES;
            if (index >= maxPieces)
            {
                failed = true;
                return false;
            }
            if (pieces[index] == nullptr)
            {
                pieces[index] = static_cast<char *>(allocator(PIECE_BYTES));
                failed = pieces[index] == nullptr;
            }
            return !failed;
        }

        size_t ChunkText::write(uint8_t c)
        {
            if (!reserveTail())
                return 0;
            pieces[length / PIECE_BYTES][length % PIECE_BYTES] = static_cast<char>(c);
            ++length;
            return 1;
        }

        size_t ChunkText::write(const uint8_t *data, size_t count)
        {
            size_t written = 0;
            while (written < count && reserveTail())
            {
                const size_t offset = length % PIECE_BYTES;
                const size_t n = std::min(count - written, PIECE_BYTES - offset);
                std::memcpy(pieces[length / PIECE_BYTES] + offset, data + written, n);
                length += n;
                written += n;
            }
            return written;
        }

        ChunkText &ChunkText::append(const char *text, size_t count)
        {
            write(reinterpret_cast<const uint8_t *>(text), count);
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
            while (copied < max && offset < length)
            {
                const size_t within = offset % PIECE_BYTES;
                const size_t n = std::min({max - copied, PIECE_BYTES - within, length - offset});
                std::memcpy(out + copied, pieces[offset / PIECE_BYTES] + within, n);
                copied += n;
                offset += n;
            }
            return copied;
        }

        void ChunkText::clear()
        {
            release();
            length = 0;
            failed = false;
        }

        std::string ChunkText::str() const
        {
            std::string out;
            for (size_t offset = 0; offset < length; offset += PIECE_BYTES)
                out.append(pieces[offset / PIECE_BYTES], std::min(PIECE_BYTES, length - offset));
            return out;
        }
    } // namespace HeapSafe
} // namespace SQM
