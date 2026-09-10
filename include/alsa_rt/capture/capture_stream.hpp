#pragma once

#include <cstdint>
#include <vector>

#include "alsa_rt/device/base.hpp"

namespace alsa_rt
{

    class CaptureStream : public BaseAlsaDevice
    {
    public:
        using RawPcmCallback = std::function<void(const uint8_t *data, size_t len)>;

        explicit CaptureStream(AlsaConfig cfg);

        void setRawPcmCallback(RawPcmCallback cb);

    protected:
        snd_pcm_stream_t stream() const override;

        bool onPreOpen() override;

        bool onPostOpen() override;

        void loop() override;

    protected:
        std::vector<uint8_t> read_buf_;
        RawPcmCallback raw_cb_;
    };

} // namespace alsa_rt
