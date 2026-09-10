#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <string_view>

#include "alsa_rt/device/alsa_config.hpp"
#include "alsa_rt/device/alsa_excption.hpp"

namespace alsa_rt
{

    /* *** pcm deleter operator *** */

    struct PcmDeleter
    {
        void operator()(snd_pcm_t *pcm) const noexcept
        {
            if (pcm)
            {
                snd_pcm_drop(pcm);
                snd_pcm_close(pcm);
            }
        }
    };

    class BaseAlsaDevice
    {
    public:
        using PcmHandle = std::unique_ptr<snd_pcm_t, PcmDeleter>;

        BaseAlsaDevice(const BaseAlsaDevice &) = delete;

        BaseAlsaDevice &operator=(const BaseAlsaDevice &) = delete;

        explicit BaseAlsaDevice(AlsaConfig cfg, int32_t rt_priority = 30, uint32_t reopen_threshold = 5);

        virtual ~BaseAlsaDevice();

        bool start();

        virtual void stop();

        void setLogSink(LogSink sink);

        const LogSink &getLogSink() const noexcept;

        const AlsaConfig &getAlsaConfig() const noexcept;

    protected:
        /* **** 子类控制 **** */

        virtual snd_pcm_stream_t stream() const = 0;

        virtual void loop() = 0;

        virtual bool onPreOpen();

        virtual bool onPostOpen();

        virtual snd_pcm_uframes_t startThreshold() const;

        virtual void onRecovered();

        virtual void onReopened();

        virtual void onDumpPcmState();

        /* **** 自恢复 **** */

        bool recover(int32_t err_code, const std::string &source);

        snd_pcm_t *pcm() const noexcept;

        uint64_t xrunCount() const noexcept;

        uint32_t consecutiveFailures() const noexcept;

        void log(LogLevel lvl, std::string_view msg) const;

    protected:
        AlsaConfig config_;
        PcmHandle pcm_;
        std::atomic<bool> running_{false};

        // rewrite defination
        snd_pcm_uframes_t period_frames_{0};
        snd_pcm_uframes_t buffer_frames_{0};
        uint32_t frame_size_{0};

    private:
        bool openDevice();

        void closeDevice();

        bool reOpen();

        void applySchedule();

    private:
        std::thread thread_;
        std::atomic<uint64_t> xrun_count_{0};
        std::atomic<uint32_t> consecutive_failures_{0};

        int32_t rt_priority_;
        uint32_t reopen_threshold_;

        LogSink log_sink_;
    };

} // namespace alsa_rt
