#include "alsa_rt/device/alsa_excption.hpp"

namespace alsa_rt::detail
{

    std::string formatXrun(int32_t err_code, const std::string &source, uint64_t total)
    {
        std::string type = (err_code == -EPIPE)      ? "xrun"
                           : (err_code == -ESTRPIPE) ? "suspend"
                                                     : "error";
        std::string s = "ALSA " + type +
                        " during " + source +
                        ": " + std::string(snd_strerror(err_code)) +
                        " (errno=" + std::to_string(err_code) +
                        ", total: " + std::to_string(total) + ")";
        return s;
    }

    std::string formatOpenInfo(uint32_t rate, uint32_t channels,
                               snd_pcm_format_t format,
                               snd_pcm_uframes_t period, snd_pcm_uframes_t buffer)
    {
        std::string s = "ALSA opened: rate=" + std::to_string(rate) +
                        ", ch=" + std::to_string(channels) +
                        ", format=" + snd_pcm_format_name(format) +
                        ", period=" + std::to_string(period) +
                        " frames(" + std::to_string(period * 1000.0 / rate) +
                        "ms), buffer=" + std::to_string(buffer) +
                        " frames(" + std::to_string(buffer * 1000.0 / rate) +
                        "ms, " + std::to_string(buffer / period) + " periods)";
        return s;
    }

    void dumpPcmState(snd_pcm_t *pcm, const std::string &tag, const LogSink &log_sink)
    {
        if (!pcm)
            return;

        snd_pcm_state_t state = snd_pcm_state(pcm);

        std::string s = "[" + tag + "] PCM state: " + snd_pcm_state_name(state) +
                        "(" + std::to_string(static_cast<int32_t>(state)) + ")";
        log_sink(LogLevel::Warn, s);

        snd_pcm_sframes_t avail = snd_pcm_avail(pcm);
        snd_pcm_sframes_t delay = 0;
        snd_pcm_delay(pcm, &delay);

        s = "[" + tag + "] avail=" + std::to_string(static_cast<long>(avail)) +
            " frames, delay=" + std::to_string(static_cast<long>(delay)) + " frames";
        log_sink(LogLevel::Warn, s);

        snd_pcm_status_t *status = nullptr;
        snd_pcm_status_malloc(&status);
        if (snd_pcm_status(pcm, status) == 0)
        {
            snd_pcm_sframes_t status_avail = snd_pcm_status_get_avail(status);

            s = "[" + tag + "] status_avail=" +
                std::to_string(static_cast<long>(status_avail));
            log_sink(LogLevel::Warn, s);

            snd_htimestamp_t trigger_ts;
            snd_pcm_status_get_trigger_htstamp(status, &trigger_ts);

            s = "[" + tag + "] trigger_tstamp: " +
                std::to_string(static_cast<long long>(trigger_ts.tv_sec)) +
                "." + std::to_string(static_cast<long long>(trigger_ts.tv_nsec));
            log_sink(LogLevel::Warn, s);
        }
        if (status)
            snd_pcm_status_free(status);
    }

} // namespace alsa_rt::detail