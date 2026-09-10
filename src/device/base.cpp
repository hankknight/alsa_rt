#include <chrono>
#include <vector>
#include <algorithm>
#include <sys/mman.h>

#include "alsa_rt/device/base.hpp"

using namespace std::chrono_literals;

namespace alsa_rt
{

    /* *** public *** */

    BaseAlsaDevice::BaseAlsaDevice(AlsaConfig cfg, int32_t rt_priority,
                                   uint32_t reopen_threshold) : config_(std::move(cfg)),
                                                                rt_priority_(rt_priority),
                                                                reopen_threshold_(reopen_threshold) {}

    BaseAlsaDevice::~BaseAlsaDevice()
    {
        stop();
    }

    bool BaseAlsaDevice::start()
    {
        if (running_)
            return true;

        if (!onPreOpen())
            return false;

        if (!openDevice())
            return false;

        if (!onPostOpen())
            return false;

        running_ = true;
        thread_ = std::thread([this]()
                              { 
                applySchedule();
                loop(); });
        return true;
    }

    void BaseAlsaDevice::stop()
    {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
        closeDevice();
    }

    void BaseAlsaDevice::setLogSink(LogSink sink) { log_sink_ = std::move(sink); }

    const LogSink &BaseAlsaDevice::getLogSink() const noexcept { return log_sink_; }

    const AlsaConfig &BaseAlsaDevice::getAlsaConfig() const noexcept { return config_; }

    /* **** action control **** */

    bool BaseAlsaDevice::onPreOpen() { return true; }

    bool BaseAlsaDevice::onPostOpen() { return true; }

    snd_pcm_uframes_t BaseAlsaDevice::startThreshold() const { return 1; }

    void BaseAlsaDevice::onRecovered() {}

    void BaseAlsaDevice::onReopened() {}

    void BaseAlsaDevice::onDumpPcmState() {}

    /* **** self recovery **** */

    bool BaseAlsaDevice::recover(int32_t err_code, const std::string &source)
    {
        xrun_count_.fetch_add(1, std::memory_order_relaxed);

        log(LogLevel::Warn, detail::formatXrun(err_code, source, xrun_count_.load()));

        onDumpPcmState();

        int32_t err = snd_pcm_recover(pcm_.get(), err_code, 1);
        if (err < 0)
        {
            std::string s = "ALSA recover failed: " + std::string(snd_strerror(err));
            log(LogLevel::Error, s);

            uint32_t fails = consecutive_failures_.fetch_add(1, std::memory_order_relaxed) + 1;
            s = "consecutive failure count=" + std::to_string(fails) +
                " (reopen threshold=" + std::to_string(reopen_threshold_) + ")";
            log(LogLevel::Warn, s);

            if (fails >= reopen_threshold_ && reOpen())
            {
                consecutive_failures_.store(0, std::memory_order_relaxed);
            }

            return false;
        }

        snd_pcm_prepare(pcm_.get());
        onRecovered();
        consecutive_failures_.store(0, std::memory_order_relaxed);
        return true;
    }

    snd_pcm_t *BaseAlsaDevice::pcm() const noexcept { return pcm_.get(); }

    uint64_t BaseAlsaDevice::xrunCount() const noexcept { return xrun_count_.load(std::memory_order_relaxed); }

    uint32_t BaseAlsaDevice::consecutiveFailures() const noexcept
    {
        return consecutive_failures_.load(std::memory_order_relaxed);
    }

    void BaseAlsaDevice::log(LogLevel lvl, std::string_view msg) const
    {
        if (log_sink_)
            log_sink_(lvl, msg);
    }

    /* *** private *** */

    bool BaseAlsaDevice::openDevice()
    {
        // init ptr
        snd_pcm_t *raw;
        int32_t err = snd_pcm_open(&raw, config_.device.c_str(), stream(), 0);

        if (err < 0)
        {
            std::string s = "snd_pcm_open failed: " + std::string(snd_strerror(err));
            log(LogLevel::Error, s);
            return false;
        }

        pcm_.reset(raw);

        // hw_params
        snd_pcm_hw_params_t *hw = nullptr;
        snd_pcm_hw_params_malloc(&hw);
        snd_pcm_hw_params_any(pcm_.get(), hw);

        snd_pcm_hw_params_set_access(pcm_.get(), hw, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm_.get(), hw, config_.format);

        uint32_t rate = config_.sample_rate;
        snd_pcm_hw_params_set_rate_near(pcm_.get(), hw, &rate, nullptr);
        snd_pcm_hw_params_set_channels(pcm_.get(), hw, config_.channels);

        int32_t dir = 0;
        snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(config_.getPeriodFrames());
        snd_pcm_hw_params_set_period_size_near(pcm_.get(), hw, &period, &dir);

        snd_pcm_uframes_t buffer = static_cast<snd_pcm_uframes_t>(config_.getBufferFrames());
        snd_pcm_hw_params_set_buffer_size_near(pcm_.get(), hw, &buffer);

        err = snd_pcm_hw_params(pcm_.get(), hw);

        // Actural hard ware params
        snd_pcm_hw_params_get_period_size(hw, &period_frames_, &dir);
        snd_pcm_hw_params_get_buffer_size(hw, &buffer_frames_);
        snd_pcm_hw_params_free(hw);

        if (err < 0)
        {
            std::string s = "snd_pcm_open failed: " + std::string(snd_strerror(err));
            log(LogLevel::Error, s);
            pcm_.reset();
            return false;
        }

        config_.updateHwParams(period_frames_, buffer_frames_);
        frame_size_ = config_.getFrameSize();

        // sw_params
        snd_pcm_sw_params_t *sw = nullptr;
        snd_pcm_sw_params_malloc(&sw);
        snd_pcm_sw_params_current(pcm_.get(), sw);

        snd_pcm_sw_params_set_start_threshold(pcm_.get(), sw, startThreshold());
        snd_pcm_sw_params_set_avail_min(pcm_.get(), sw, period_frames_);

        if (snd_pcm_sw_params(pcm_.get(), sw) < 0)
        {
            log(LogLevel::Warn, std::string_view("sw_params set failed (non-fatal)"));
        }
        snd_pcm_sw_params_free(sw);

        snd_pcm_prepare(pcm_.get());

        // debug
        log(LogLevel::Info, detail::formatOpenInfo(rate, config_.channels, config_.format,
                                                   period_frames_, buffer_frames_));

        return true;
    }

    void BaseAlsaDevice::closeDevice()
    {
        pcm_.reset();
    }

    bool BaseAlsaDevice::reOpen()
    {
        std::string s = "Reopening ALSA device '" +
                        config_.device +
                        "' due to persistent xrun/recover failure...";
        log(LogLevel::Warn, s);

        closeDevice();

        auto delay = 500ms;
        auto max_delay = 5000ms;
        while (running_)
        {
            if (openDevice())
            {
                s = "ALSA device reopened successfully: " + config_.device;
                log(LogLevel::Info, s);
                onReopened();
                return true;
            }

            s = "reopen '" + config_.device + "' failed, retry in " +
                std::to_string(delay.count()) + "ms (running=" +
                (running_.load() ? "T" : "F") + ")";
            log(LogLevel::Warn, s);

            auto end = std::chrono::steady_clock::now() + delay;
            while (running_ && std::chrono::steady_clock::now() < end)
            {
                std::this_thread::sleep_for(100ms);
            }

            delay = std::min(delay * 2, max_delay);
        }

        log(LogLevel::Warn, std::string_view("reopen aborted, device loop is stopping"));
        return false;
    }

    void BaseAlsaDevice::applySchedule()
    {
        struct sched_param param = {.sched_priority = rt_priority_};
        int32_t ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        if (ret != 0)
        {
            std::string s = "Failed to set SCHED_FIFO (need root/CAP_SYS_NICE): " + std::string(strerror(ret));
            log(LogLevel::Warn, s);
        }
        else
        {
            std::string s = "SCHED_FIFO priority=" + std::to_string(rt_priority_) +
                            " set successfully (tid=" + std::to_string(pthread_self()) + ")";
            log(LogLevel::Info, s);
        }

        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        {
            std::string s = "mlockall failed (need root/CAP_IPC_LOCK): " + std::string(strerror(errno));
            log(LogLevel::Warn, s);
        }
        else
        {
            log(LogLevel::Info, std::string_view("mlockall set successfully"));
        }
    }

} // namespace alsa_rt
