#include <chrono>
#include <string>

#include "alsa_rt/playback/playback_stream.hpp"

using namespace std::chrono_literals;

namespace alsa_rt
{

    PlayBackStream::PlayBackStream(AlsaConfig cfg) : BaseAlsaDevice(cfg) {}

    void PlayBackStream::feed(const std::vector<uint8_t> &frame_data)
    {
        feed(frame_data.data(), frame_data.size());
    }

    void PlayBackStream::feed(const uint8_t *data, size_t len)
    {
        if (len == 0 || !data || !data_ring_)
            return;

        data_ring_->pushDiscard(data, len);
    }

    void PlayBackStream::flush()
    {
        if (!data_ring_)
            return;

        data_ring_->clearRing();
    }

    void PlayBackStream::setBufferSeconds(size_t seconds) { buffer_second_ = seconds; }

    snd_pcm_stream_t PlayBackStream::stream() const { return SND_PCM_STREAM_PLAYBACK; }

    snd_pcm_uframes_t PlayBackStream::startThreshold() const { return period_frames_; }

    bool PlayBackStream::onPreOpen()
    {
        data_ring_ = std::make_unique<SpscByteRing>(config_.getByteRate() * buffer_second_);

        std::string s = "data_ring capacity: " + std::to_string(data_ring_->capacity()) +
                        " bytes (" + std::to_string(static_cast<double>(buffer_second_)) +
                        "s @ " + std::to_string(config_.sample_rate) +
                        "Hz/" + std::to_string(config_.channels) +
                        "ch/" + std::to_string(config_.getSampleBytes()) + "bytes)";
        log(LogLevel::Info, s);
        return true;
    }

    bool PlayBackStream::onPostOpen()
    {
        write_buffer_.assign(config_.getPeriodBytes(), 0);
        return true;
    }

    void PlayBackStream::onRecovered() { writeSilence(); }

    void PlayBackStream::onReopened() { writeSilence(); }

    void PlayBackStream::writeSilence()
    {
        std::fill(write_buffer_.begin(), write_buffer_.end(), 0);
        snd_pcm_writei(pcm(), write_buffer_.data(), period_frames_);
    }

    int32_t PlayBackStream::writeFrames(const uint8_t *data, int32_t frames)
    {
        if (!pcm() || !data || frames <= 0)
            return -1;

        const char *ptr = reinterpret_cast<const char *>(data);
        int32_t remaining = frames;
        while (remaining > 0)
        {
            snd_pcm_sframes_t written = snd_pcm_writei(pcm(), ptr, remaining);
            if (written < 0)
                return static_cast<int32_t>(written);

            if (written == 0)
            {
                std::this_thread::sleep_for(1ms);
                continue;
            }

            ptr += written * frame_size_;
            remaining -= static_cast<int32_t>(written);
        }
        return frames;
    }

    void PlayBackStream::loop()
    {
        std::string s = "Write loop started (tid=" + std::to_string(pthread_self()) + ")";
        log(LogLevel::Info, s);

        const size_t period_bytes = config_.getPeriodBytes();

        while (running_)
        {
            // Read one period from ring and pad silence for missing bytes.
            size_t available = data_ring_->popAvailable();
            size_t to_read = std::min(available, period_bytes);

            if (to_read > 0)
                data_ring_->pop(write_buffer_.data(), to_read);

            if (to_read < period_bytes)
                std::fill(write_buffer_.begin() + to_read, write_buffer_.end(), 0);

            int32_t err = writeFrames(write_buffer_.data(), static_cast<int32_t>(period_frames_));
            if (err < 0)
            {
                if (!recover(err, "write"))
                {
                    continue;
                }

                std::this_thread::sleep_for(10ms);
                continue;
            }
        }

        s = "Write loop ended, xruns=" + std::to_string(xrunCount());
        log(LogLevel::Info, s);
    }

} // namespace alsa_rt
