#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace alsa_rt
{
    class SpscByteRing
    {
    public:
        explicit SpscByteRing(size_t capacity);

        ~SpscByteRing() = default;

        /* *** attribute *** */

        size_t capacity() const;

        size_t popAvailable() const;

        size_t curWritePos() const;

        /* *** operate *** */

        void pushDiscard(const uint8_t *data, size_t len);

        size_t pop(uint8_t *out, size_t max_len);

        // shift to target buffer
        void skipTo(size_t target_pos);

        void clearRing();

    private:
        void writeAt(size_t pos, const uint8_t *data, size_t len);

        void readAt(size_t pos, uint8_t *out, size_t len);

    private:
        size_t mask_{0};
        std::vector<uint8_t> buffer_;

        alignas(64) std::atomic<size_t> write_pos_{0};
        alignas(64) std::atomic<size_t> read_pos_{0};
    };

} // namespace alsa_rt
