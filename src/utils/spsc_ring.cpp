#include <algorithm>
#include <cstring>

#include "alsa_rt/utils/spsc_ring.hpp"

namespace alsa_rt
{

    /* ************************* public ************************* */

    SpscByteRing::SpscByteRing(size_t capacity)
    {
        size_t pow2 = 1;
        while (pow2 < capacity)
            pow2 <<= 1;
        mask_ = pow2 - 1;
        buffer_.resize(pow2, 0);
    }

    size_t SpscByteRing::capacity() const { return mask_ + 1; }

    size_t SpscByteRing::popAvailable() const
    {
        return write_pos_.load(std::memory_order_acquire) -
               read_pos_.load(std::memory_order_relaxed);
    }

    size_t SpscByteRing::curWritePos() const
    {
        return write_pos_.load(std::memory_order_acquire);
    }

    // 满时丢旧, 保留最新。生产端独占调用。
    void SpscByteRing::pushDiscard(const uint8_t *data, size_t len)
    {
        const size_t cap = mask_ + 1;
        const size_t write_pos = write_pos_.load(std::memory_order_relaxed);
        size_t read_pos = read_pos_.load(std::memory_order_acquire);

        if (write_pos - read_pos + len > cap)
        {
            // 腾出刚好够 len 的空间, 多余的旧数据丢弃
            size_t need = (write_pos - read_pos + len) - cap;
            read_pos_.store(read_pos + need, std::memory_order_release);
        }
        writeAt(write_pos, data, len);
        write_pos_.store(write_pos + len, std::memory_order_release);
    }

    size_t SpscByteRing::pop(uint8_t *out, size_t max_len)
    {
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);
        const size_t read_pos = read_pos_.load(std::memory_order_relaxed);
        const size_t len = std::min(write_pos - read_pos, max_len);
        if (len == 0)
            return 0;
        readAt(read_pos, out, len);
        read_pos_.store(read_pos + len, std::memory_order_release);
        return len;
    }

    // 消费侧: 直接跳到 target_pos, 丢弃 target_pos 之前的所有数据。
    // target_pos 应来自 curWritePos() 快照, 越界会 clamp 到当前 write_pos。
    void SpscByteRing::skipTo(size_t target_pos)
    {
        const size_t write_pos = write_pos_.load(std::memory_order_acquire);
        if (target_pos > write_pos)
            target_pos = write_pos;
        read_pos_.store(target_pos, std::memory_order_release);
    }

    // 消费侧清空 (读到 write_pos)
    void SpscByteRing::clearRing()
    {
        read_pos_.store(write_pos_.load(std::memory_order_acquire),
                        std::memory_order_release);
    }

    /* ************************* private ************************* */

    void SpscByteRing::writeAt(size_t pos, const uint8_t *data, size_t len)
    {
        const size_t cap = mask_ + 1;
        const size_t start = pos & mask_;
        const size_t first = (start + len <= cap) ? len : cap - start;
        std::memcpy(buffer_.data() + start, data, first);
        if (first < len)
            std::memcpy(buffer_.data(), data + first, len - first);
    }

    void SpscByteRing::readAt(size_t pos, uint8_t *out, size_t len)
    {
        const size_t cap = mask_ + 1;
        const size_t start = pos & mask_;
        const size_t first = (start + len <= cap) ? len : cap - start;
        std::memcpy(out, buffer_.data() + start, first);
        if (first < len)
            std::memcpy(out + first, buffer_.data(), len - first);
    }

} // namespace alsa_rt
