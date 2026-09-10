#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include <alsa/asoundlib.h>

extern "C"
{
#include <libavutil/samplefmt.h>
}

#include "alsa_rt/device/alsa_config.hpp"

namespace alsa_rt
{

    class AlsaToFFmpegFormater
    {
    public:
        AlsaToFFmpegFormater() = delete;

        ~AlsaToFFmpegFormater() = default;

        explicit AlsaToFFmpegFormater(const AlsaConfig &source_cfg);

        AVSampleFormat getAvFormat() const noexcept;

        const AlsaConfig &getEffectiveConfig() const noexcept;

        bool needsConvert() const noexcept;

        std::vector<uint8_t> &scratch() noexcept;

        const uint8_t *normalize(const uint8_t *src, size_t frames, size_t &out_len);

        static AVSampleFormat resolveAvFormat(snd_pcm_format_t alsa_format);

    protected:
        void resolveTargetFormat();

        void reserveScratch();

    private:
        using ConvertFunc = void (*)(const uint8_t *, size_t, uint8_t *);

        struct ConversionInfo
        {
            snd_pcm_format_t target_format;
            ConvertFunc convert_func;
        };

        static const std::unordered_map<snd_pcm_format_t, AVSampleFormat> kAlsaToFFmpegFormatMap;
        static const std::unordered_map<snd_pcm_format_t, ConversionInfo> kAlsaToTargetFormatMap;

        AlsaConfig effective_cfg_;
        bool needs_convert_{false};
        std::vector<uint8_t> scratch_;
        ConvertFunc convert_func_{nullptr};
    };

} // namespace alsa_rt
