#include <cmath>
#include <stdexcept>

#include "alsa_rt/device/alsa_config.hpp"

namespace alsa_rt
{

    AlsaConfig::AlsaConfig(const std::string &device, uint32_t channels,
                           uint32_t sample_rate, snd_pcm_format_t format, double period_ms,
                           uint8_t buffer_periods) : device(device), channels(channels),
                                                     sample_rate(sample_rate),
                                                     format(format),
                                                     period_ms(period_ms),
                                                     buffer_period_count_(buffer_periods)

    {
        checkParams();
        calcDeviceParams();
    }

    uint64_t AlsaConfig::getPeriodFrames() const noexcept { return period_size_; }

    uint64_t AlsaConfig::getBufferFrames() const noexcept { return buffer_size_; }

    uint32_t AlsaConfig::getFrameSize() const noexcept { return frame_size_; }

    uint64_t AlsaConfig::getByteRate() const noexcept { return byte_rate_; }

    /* ** for debug check ** */

    uint64_t AlsaConfig::getPeriodBytes() const noexcept { return period_bytes_; }

    uint32_t AlsaConfig::getSampleBytes() const noexcept { return sample_bytes_; }

    int32_t AlsaConfig::getBitsPerSample() const noexcept
    {
        return snd_pcm_format_physical_width(format);
    }

    // acture param from device
    void AlsaConfig::updateHwParams(uint64_t period_size, uint64_t buffer_size)
    {
        period_size_ = period_size;
        buffer_size_ = buffer_size;
        period_bytes_ = period_size_ * frame_size_;
    }

    void AlsaConfig::checkParams()
    {
        if (channels == 0)
        {
            throw std::invalid_argument("channels must be > 0");
        }
        if (sample_rate == 0)
        {
            throw std::invalid_argument("sample_rate must be > 0");
        }
        if (period_ms <= 0.0)
        {
            throw std::invalid_argument("period_ms must be > 0");
        }
    }

    void AlsaConfig::calcDeviceParams()
    {
        int32_t bits = snd_pcm_format_physical_width(format);
        if (bits <= 0 || (bits % 8) != 0)
        {
            throw std::runtime_error("Unsupported PCM format: " +
                                     std::to_string(static_cast<int32_t>(format)));
        }

        // frame size unit
        sample_bytes_ = static_cast<uint32_t>(bits / 8);
        frame_size_ = sample_bytes_ * channels;

        // byte rate (PCM bytes per second)
        byte_rate_ = static_cast<uint64_t>(sample_rate) * frame_size_;

        // period and buffer
        period_size_ = static_cast<uint64_t>(std::round(sample_rate * period_ms / 1000.0));
        buffer_size_ = period_size_ * buffer_period_count_;
        period_bytes_ = period_size_ * frame_size_;
    }

} // namespace alsa_rt
