#pragma once

#include <variant>

#include "alsa_rt/resampler/base.hpp"

namespace alsa_rt
{

    struct SingleModeParams
    {
        int32_t selected_channel;
    };

    struct SubsetModeParams
    {
        uint32_t out_channel;
        std::vector<int32_t> channel_map;
    };

    struct AllModeParams
    {
    };

    struct ResamplerConfig
    {
        uint32_t out_sample_rate;
        AVSampleFormat out_format;
        std::variant<SingleModeParams, SubsetModeParams, AllModeParams> params;
    };

    class SingleChannelResampler : public BaseResampler
    {
    public:
        explicit SingleChannelResampler(const AlsaConfig &cfg);

        void setup(uint32_t out_sample_rate,
                   AVSampleFormat out_format,
                   int32_t selected_channel);
    };

    class SubsetChannelResampler : public BaseResampler
    {
    public:
        explicit SubsetChannelResampler(const AlsaConfig &cfg);

        void setup(uint32_t out_sample_rate,
                   uint32_t out_channel,
                   AVSampleFormat out_format,
                   const std::vector<int32_t> &channel_map);
    };

    class AllChannelResampler : public BaseResampler
    {
    public:
        explicit AllChannelResampler(const AlsaConfig &cfg);

        void setup(uint32_t out_sample_rate, AVSampleFormat out_format);
    };

    // factory function to create resampler instance
    std::unique_ptr<BaseResampler> createResampler(const AlsaConfig &cfg, const ResamplerConfig &resampler_cfg);

} // namespace alsa_rt