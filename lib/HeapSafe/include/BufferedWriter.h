#pragma once

// Spec 031: ArduinoJson writes punctuation one byte at a time; a network
// client would send each as its own write. This batches them in a small
// stack buffer, so a document streams to MQTT with no heap at all.
// `Out` needs `size_t write(const uint8_t *, size_t)`.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SQM
{
    namespace HeapSafe
    {
        template <typename Out, size_t N = 128>
        class BufferedWriter
        {
        public:
            explicit BufferedWriter(Out &out) : out_(out) {}
            BufferedWriter(const BufferedWriter &) = delete;
            BufferedWriter &operator=(const BufferedWriter &) = delete;

            size_t write(uint8_t c)
            {
                if (used_ == N && !flush())
                    return 0;
                buffer_[used_++] = c;
                return 1;
            }

            size_t write(const uint8_t *data, size_t length)
            {
                for (size_t i = 0; i < length; ++i)
                    if (write(data[i]) == 0)
                        return i;
                return length;
            }

            // Sends what is held; false once any write came back short.
            bool flush()
            {
                if (used_ > 0 && !failed_)
                    failed_ = out_.write(buffer_, used_) != used_;
                used_ = 0;
                return !failed_;
            }

            bool failed() const { return failed_; }

        private:
            Out &out_;
            uint8_t buffer_[N];
            size_t used_ = 0;
            bool failed_ = false;
        };
    } // namespace HeapSafe
} // namespace SQM
