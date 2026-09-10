#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include <stdexcept>

extern "C"
{
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

#include "alsa_rt/resampler/format/format.hpp"

namespace alsa_rt
{
    struct SwrDeleter
    {
        void operator()(SwrContext *ctx) const { swr_free(&ctx); }
    };

    class BaseResampler
    {
    public:
        using SwrContextPtr = std::unique_ptr<SwrContext, SwrDeleter>;

        BaseResampler(const BaseResampler &) = delete;

        BaseResampler &operator=(const BaseResampler &) = delete;

        explicit BaseResampler(const AlsaConfig &cfg);

        virtual ~BaseResampler() = default;

        void setOutputParameters(uint32_t out_sample_rate,
                                 uint32_t out_channel,
                                 AVSampleFormat out_format);

        bool init();

        bool isInitialized() const noexcept;

        // set input channel matrix, shape [out_channels * in_channels]
        bool setChannelSelect(const std::vector<int32_t> &channel_indices);

        int32_t maxOutputSamples(int32_t input_samples) const;

        /**
         * @brief audio stream post-process
         *
         * @param input raw audio stream
         * @param input_samples sample bytes per channel(frame)
         * @param output resample audio stream
         * @param output_capacity output buffer bytes capacity
         * @return int32_t  > 0 : actural output bytes
         *                  -1  : param error or init failed
         *                  -2  : allocate output buffer not enough
         *                  < -2: FFmpeg internal error code
         *
         */
        int32_t convert(const uint8_t *input,
                        int32_t input_samples,
                        uint8_t *output,
                        int32_t output_capacity);

    protected:
        AlsaToFFmpegFormater formater_;

        const uint32_t in_channels_;
        const uint32_t in_sample_rate_;
        const AVSampleFormat in_format_;

        int32_t frame_bytes_ = 0;
        uint32_t out_sample_rate_ = 0;
        uint32_t out_channel_ = 0;
        AVSampleFormat out_format_ = AV_SAMPLE_FMT_NONE;

        bool initialized_{false};
        std::vector<double> matrix_;
        SwrContextPtr ctx_;
    };

} // namespace alsa_rt