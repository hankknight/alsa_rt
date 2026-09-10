#pragma once

#include <string>
#include <cstdint>
#include <alsa/asoundlib.h>

namespace alsa_rt
{

    class AlsaConfig
    {
    public:
        AlsaConfig() = default;

        ~AlsaConfig() = default;

        AlsaConfig(const std::string &device, uint32_t channels,
                   uint32_t sample_rate, snd_pcm_format_t format, double period_ms,
                   uint8_t buffer_periods = 2);

        uint64_t getPeriodFrames() const noexcept;

        uint64_t getBufferFrames() const noexcept;

        uint32_t getFrameSize() const noexcept;

        uint64_t getByteRate() const noexcept;

        /* ** for debug check ** */

        uint64_t getPeriodBytes() const noexcept;

        uint32_t getSampleBytes() const noexcept;

        int32_t getBitsPerSample() const noexcept;

        // acture param from device
        void updateHwParams(uint64_t period_size, uint64_t buffer_size);

    protected:
        void checkParams();

        void calcDeviceParams();

    public:
        std::string device;
        uint32_t sample_rate;
        uint32_t channels;
        double period_ms;
        snd_pcm_format_t format;

    private:
        uint8_t buffer_period_count_{2};

        uint64_t byte_rate_{0};
        uint64_t period_bytes_{0};
        uint64_t period_size_{0};
        uint64_t buffer_size_{0};
        uint32_t sample_bytes_{0};
        uint32_t frame_size_{0};
    };

} // namespace alsa_rt
