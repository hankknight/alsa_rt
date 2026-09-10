#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>

#include "alsa_rt/device/base.hpp"
#include "alsa_rt/utils/spsc_ring.hpp"

namespace alsa_rt
{

    // standard playback
    class PlayBackStream : public BaseAlsaDevice
    {
    public:
        explicit PlayBackStream(AlsaConfig cfg);

        void feed(const std::vector<uint8_t> &frame_data);

        void feed(const uint8_t *data, size_t len);

        void flush();

        // decide audio cached duration
        void setBufferSeconds(size_t seconds);

    protected:
        snd_pcm_stream_t stream() const override;

        snd_pcm_uframes_t startThreshold() const override;

        bool onPreOpen() override;

        bool onPostOpen() override;

        void onRecovered() override;

        void onReopened() override;

        void writeSilence();

        int32_t writeFrames(const uint8_t *data, int32_t frames);

        void loop() override;

    protected:
        std::unique_ptr<SpscByteRing> data_ring_;
        std::vector<uint8_t> write_buffer_;

        size_t buffer_second_{1};
    };

    // with priority playback
    class PlayBackPriorityStream : public BaseAlsaDevice
    {
    public:
        explicit PlayBackPriorityStream(AlsaConfig cfg);

        void feed(int32_t priority, const std::vector<uint8_t> &frame_data);

        void feed(int32_t priority, const uint8_t *data, size_t len);

        void flush();

        void setBufferSeconds(size_t seconds);

    protected:
        snd_pcm_stream_t stream() const override;

        snd_pcm_uframes_t startThreshold() const override;

        bool onPreOpen() override;

        bool onPostOpen() override;

        void onRecovered() override;

        void onReopened() override;

        void writeSilence();

        int32_t writeFrames(const uint8_t *data, int32_t frames);

        void loop() override;

    protected:
        std::unique_ptr<SpscByteRing> data_ring_;
        std::vector<uint8_t> write_buffer_;

        size_t buffer_second_{1};

        std::mutex feed_mtx_;
        std::atomic<size_t> skip_to_pos_{0};
        std::atomic<size_t> current_priority_{0};
    };

} // namespace alsa_rt
