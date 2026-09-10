#include <cstddef>

extern "C"
{
#include <libavutil/version.h>
}

#include "alsa_rt/resampler/base.hpp"
#include "alsa_rt/resampler/format/format.hpp"

namespace alsa_rt
{
    BaseResampler::BaseResampler(const AlsaConfig &cfg) : formater_(cfg),
                                                          in_channels_(formater_.getEffectiveConfig().channels),
                                                          in_sample_rate_(formater_.getEffectiveConfig().sample_rate),
                                                          in_format_(formater_.getAvFormat())
    {
    }

    void BaseResampler::setOutputParameters(uint32_t out_sample_rate,
                                            uint32_t out_channel,
                                            AVSampleFormat out_format)
    {
        if (av_sample_fmt_is_planar(out_format))
        {
            throw std::invalid_argument(
                "BaseResampler does not support planar output formats");
        }

        out_sample_rate_ = out_sample_rate;
        out_channel_ = out_channel;
        out_format_ = out_format;
        frame_bytes_ = av_get_bytes_per_sample(out_format) *
                       static_cast<int32_t>(out_channel_);
    }

    bool BaseResampler::init()
    {
        if (initialized_)
            return true;

        int32_t ret = 0;
        SwrContext *raw = nullptr;

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100) // FFmpeg 5.1+
        // latest release channel layout API (FFmpeg 5.1+)
        AVChannelLayout in_layout, out_layout;
        av_channel_layout_default(&in_layout, static_cast<int32_t>(in_channels_));
        av_channel_layout_default(&out_layout, static_cast<int32_t>(out_channel_));

        ret = swr_alloc_set_opts2(&raw,
                                  &out_layout,
                                  out_format_,
                                  static_cast<int32_t>(out_sample_rate_),
                                  &in_layout,
                                  in_format_,
                                  static_cast<int32_t>(in_sample_rate_),
                                  0, nullptr);

        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);
#else
        // old release channel layout API (FFmpeg < 5.1)
        const uint64_t in_ch_layout = av_get_default_channel_layout(static_cast<int32_t>(in_channels_));
        const uint64_t out_ch_layout = av_get_default_channel_layout(static_cast<int32_t>(out_channel_));

        raw = swr_alloc_set_opts(nullptr,
                                 out_ch_layout, out_format_, static_cast<int32_t>(out_sample_rate_),
                                 in_ch_layout, in_format_, static_cast<int32_t>(in_sample_rate_),
                                 0, nullptr);
        ret = raw ? 0 : -1;
#endif

        if (ret < 0 || !raw)
            return false;

        SwrContextPtr ctx(raw);

        av_opt_set_int(ctx.get(), "dither_method", SWR_DITHER_NONE, 0);
        av_opt_set_int(ctx.get(), "filter_size", 8, 0);
        av_opt_set_int(ctx.get(), "linear_interp", 1, 0);

        if (!matrix_.empty())
        {
            if (swr_set_matrix(ctx.get(), matrix_.data(), static_cast<int32_t>(in_channels_)) < 0)
                return false;
        }

        if (swr_init(ctx.get()) < 0)
        {
            return false;
        }

        ctx_ = std::move(ctx);
        initialized_ = true;
        return true;
    }

    bool BaseResampler::setChannelSelect(const std::vector<int32_t> &channel_indices)
    {
        if (initialized_ || channel_indices.size() != out_channel_)
            return false;

        matrix_.assign(static_cast<size_t>(out_channel_) * in_channels_, 0.0);
        for (size_t o = 0; o < channel_indices.size(); ++o)
        {
            const int32_t in_ch = channel_indices[o];
            if (in_ch < 0 || static_cast<uint32_t>(in_ch) >= in_channels_)
                return false;
            matrix_[o * in_channels_ + in_ch] = 1.0;
        }

        return true;
    }

    int32_t BaseResampler::maxOutputSamples(int32_t input_samples) const
    {
        if (in_sample_rate_ == out_sample_rate_)
            return input_samples;

        return static_cast<int32_t>(
            av_rescale_rnd(input_samples, out_sample_rate_, in_sample_rate_, AV_ROUND_UP));
    }

    int32_t BaseResampler::convert(const uint8_t *input,
                                   int32_t input_samples,
                                   uint8_t *output,
                                   int32_t output_capacity)
    {
        if (!ctx_ || !input || !output || input_samples <= 0 || output_capacity <= 0)
            return -1;

        const int32_t out_samples = maxOutputSamples(input_samples);
        const int32_t required_bytes = out_samples * frame_bytes_;
        if (output_capacity < required_bytes)
            return -2;

        // format normalize（e.g. S24_3LE -> S32LE）
        size_t normalized_len = 0;
        const uint8_t *in_ptr = formater_.normalize(input, input_samples, normalized_len);

        uint8_t *dst_ptr = output;
        const int32_t converted_samples =
            swr_convert(ctx_.get(), &dst_ptr, out_samples, &in_ptr, input_samples);

        if (converted_samples < 0)
            return converted_samples;

        return converted_samples * frame_bytes_;
    }

    bool BaseResampler::isInitialized() const noexcept { return initialized_; }

} // namespace alsa_rt
