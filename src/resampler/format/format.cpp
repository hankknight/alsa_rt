#include <stdexcept>

#include "alsa_rt/resampler/format/format.hpp"
#include "alsa_rt/resampler/format/unpack.hpp"

namespace alsa_rt
{

    // can be expended if needed
    const std::unordered_map<snd_pcm_format_t, AVSampleFormat>
        AlsaToFFmpegFormater::kAlsaToFFmpegFormatMap = {
            {SND_PCM_FORMAT_S16_LE, AV_SAMPLE_FMT_S16},
            {SND_PCM_FORMAT_S32_LE, AV_SAMPLE_FMT_S32},
            {SND_PCM_FORMAT_FLOAT_LE, AV_SAMPLE_FMT_FLT},
    };

    const std::unordered_map<snd_pcm_format_t, AlsaToFFmpegFormater::ConversionInfo>
        AlsaToFFmpegFormater::kAlsaToTargetFormatMap = {
            {SND_PCM_FORMAT_S24_3LE, {SND_PCM_FORMAT_S32_LE, unpack::S24_3LEToS32LE}},
    };

    /* *** public *** */

    AlsaToFFmpegFormater::AlsaToFFmpegFormater(const AlsaConfig &source_cfg)
        : effective_cfg_(source_cfg)
    {
        resolveTargetFormat();
        reserveScratch();
    }

    AVSampleFormat AlsaToFFmpegFormater::getAvFormat() const noexcept
    {
        auto it = kAlsaToFFmpegFormatMap.find(effective_cfg_.format);
        return (it != kAlsaToFFmpegFormatMap.end()) ? it->second : AV_SAMPLE_FMT_NONE;
    }

    const AlsaConfig &AlsaToFFmpegFormater::getEffectiveConfig() const noexcept
    {
        return effective_cfg_;
    }

    bool AlsaToFFmpegFormater::needsConvert() const noexcept { return needs_convert_; }

    std::vector<uint8_t> &AlsaToFFmpegFormater::scratch() noexcept { return scratch_; }

    const uint8_t *AlsaToFFmpegFormater::normalize(const uint8_t *src, size_t frames, size_t &out_len)
    {
        const size_t total_samples = frames * effective_cfg_.channels;
        const size_t target_bytes = effective_cfg_.getSampleBytes();
        out_len = total_samples * target_bytes;

        if (!needs_convert_)
            return src;

        if (!convert_func_)
            throw std::runtime_error("no converter registered for format");

        if (out_len > scratch_.size())
        {
            throw std::runtime_error("ffmpeg converter scratch overflow: need " +
                                     std::to_string(out_len) + ", have " +
                                     std::to_string(scratch_.size()));
        }

        convert_func_(src, total_samples, scratch_.data());
        return scratch_.data();
    }

    AVSampleFormat AlsaToFFmpegFormater::resolveAvFormat(snd_pcm_format_t alsa_format)
    {
        // 1. ffmpeg raw format
        if (auto it = kAlsaToFFmpegFormatMap.find(alsa_format); it != kAlsaToFFmpegFormatMap.end())
        {
            return it->second;
        }

        // 2. normalize format mapping, such as S24_3LE -> S32_LE
        if (auto it = kAlsaToTargetFormatMap.find(alsa_format); it != kAlsaToTargetFormatMap.end())
        {
            const auto target_it = kAlsaToFFmpegFormatMap.find(it->second.target_format);
            if (target_it != kAlsaToFFmpegFormatMap.end())
            {
                return target_it->second;
            }
        }

        // current format not support
        return AV_SAMPLE_FMT_NONE;
    }

    /* *** protected *** */

    void AlsaToFFmpegFormater::resolveTargetFormat()
    {
        const auto it = kAlsaToTargetFormatMap.find(effective_cfg_.format);
        if (it == kAlsaToTargetFormatMap.end())
            return;

        effective_cfg_ = AlsaConfig(
            effective_cfg_.device,
            effective_cfg_.channels,
            effective_cfg_.sample_rate,
            it->second.target_format,
            effective_cfg_.period_ms);

        convert_func_ = it->second.convert_func;
        needs_convert_ = true;
    }

    void AlsaToFFmpegFormater::reserveScratch()
    {
        if (!needs_convert_)
        {
            scratch_.clear();
            return;
        }
        const size_t max_samples = effective_cfg_.getPeriodFrames() * effective_cfg_.channels;
        const size_t target_bytes = effective_cfg_.getSampleBytes();
        scratch_.assign(max_samples * target_bytes, 0);
    }

} // namespace alsa_rt
