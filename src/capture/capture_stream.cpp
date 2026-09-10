#include <chrono>
#include <string>

#include "alsa_rt/capture/capture_stream.hpp"

using namespace std::chrono_literals;

namespace alsa_rt
{

    CaptureStream::CaptureStream(AlsaConfig cfg) : BaseAlsaDevice(std::move(cfg)) {}

    void CaptureStream::setRawPcmCallback(RawPcmCallback cb) { raw_cb_ = std::move(cb); }

    snd_pcm_stream_t CaptureStream::stream() const { return SND_PCM_STREAM_CAPTURE; }

    bool CaptureStream::onPreOpen()
    {
        if (!raw_cb_)
        {
            log(LogLevel::Error, std::string_view("raw cb not set before start -- refusing to open device"));
            return false;
        }
        return true;
    }

    bool CaptureStream::onPostOpen()
    {
        read_buf_.assign(config_.getPeriodBytes(), 0);
        std::string s = "read_buf capacity: " + std::to_string(read_buf_.size()) +
                        " bytes (period=" + std::to_string(period_frames_) +
                        " frames, frame_size=" + std::to_string(frame_size_) +
                        ")";
        log(LogLevel::Info, s);
        return true;
    }

    void CaptureStream::loop()
    {
        std::string s = "Capture loop started (tid=" + std::to_string(pthread_self()) + ")";
        log(LogLevel::Info, s);

        while (running_)
        {
            snd_pcm_sframes_t frames = snd_pcm_readi(pcm(), read_buf_.data(), period_frames_);

            if (frames < 0)
            {
                if (!recover(static_cast<int>(frames), "read"))
                {
                    continue;
                }

                std::this_thread::sleep_for(10ms);
                continue;
            }

            const size_t raw_len = static_cast<size_t>(frames) * frame_size_;
            raw_cb_(read_buf_.data(), raw_len);
        }

        s = "Capture loop ended, xruns=" + std::to_string(xrunCount());
        log(LogLevel::Info, s);
    }

} // namespace alsa_rt
