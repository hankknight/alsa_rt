#include "alsa_rt/resampler/resampler.hpp"

namespace alsa_rt
{

    SingleChannelResampler::SingleChannelResampler(const AlsaConfig &cfg) : BaseResampler(cfg) {}

    void SingleChannelResampler::setup(uint32_t out_sample_rate,
                                       AVSampleFormat out_format,
                                       int32_t selected_channel)
    {
        setOutputParameters(out_sample_rate, 1, out_format);

        if (!setChannelSelect({selected_channel}))
        {
            throw std::runtime_error(
                "SingleChannelResampler: selected_channel " + std::to_string(selected_channel) +
                " out of range or invalid (input channels: " +
                std::to_string(in_channels_) + ")");
        }
    }

    SubsetChannelResampler::SubsetChannelResampler(const AlsaConfig &cfg) : BaseResampler(cfg) {}

    void SubsetChannelResampler::setup(uint32_t out_sample_rate,
                                       uint32_t out_channel,
                                       AVSampleFormat out_format,
                                       const std::vector<int32_t> &channel_map)
    {
        setOutputParameters(out_sample_rate, out_channel, out_format);

        if (!setChannelSelect(channel_map))
        {
            throw std::runtime_error("SubsetChannelResampler: invalid channel map");
        }
    }

    AllChannelResampler::AllChannelResampler(const AlsaConfig &cfg) : BaseResampler(cfg) {}

    void AllChannelResampler::setup(uint32_t out_sample_rate, AVSampleFormat out_format)
    {
        setOutputParameters(out_sample_rate, in_channels_, out_format);
    }

    namespace
    {
        template <class... Ts>
        struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <class... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;
    }

    std::unique_ptr<BaseResampler> createResampler(const AlsaConfig &cfg, const ResamplerConfig &resampler_cfg)
    {
        return std::visit(
            Overloaded{
                [&](const SingleModeParams &p) -> std::unique_ptr<BaseResampler>
                {
                    auto resampler = std::make_unique<SingleChannelResampler>(cfg);
                    resampler->setup(resampler_cfg.out_sample_rate, resampler_cfg.out_format, p.selected_channel);
                    return resampler;
                },
                [&](const SubsetModeParams &p) -> std::unique_ptr<BaseResampler>
                {
                    if (p.channel_map.empty())
                        throw std::invalid_argument("createResampler: kSubset requires non-empty channel_map");

                    auto resampler = std::make_unique<SubsetChannelResampler>(cfg);
                    resampler->setup(resampler_cfg.out_sample_rate, p.out_channel, resampler_cfg.out_format, p.channel_map);
                    return resampler;
                },
                [&](const AllModeParams &) -> std::unique_ptr<BaseResampler>
                {
                    auto resampler = std::make_unique<AllChannelResampler>(cfg);
                    resampler->setup(resampler_cfg.out_sample_rate, resampler_cfg.out_format);
                    return resampler;
                },
            },
            resampler_cfg.params);
    }

} // namespace alsa_rt
