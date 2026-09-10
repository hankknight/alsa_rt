#include <chrono>
#include <thread>
#include <utility>

#include "alsa_rt/capture/capture_stream_debug.hpp"

using namespace std::chrono_literals;

namespace alsa_rt
{

    CaptureStreamDebug::CaptureStreamDebug(AlsaConfig cfg) : CaptureStream(std::move(cfg)) {}

    void CaptureStreamDebug::loop()
    {
        const auto period_frames = static_cast<snd_pcm_uframes_t>(config_.getPeriodFrames());
        const auto buffer_frames = config_.getBufferFrames();
        const auto frame_size = config_.getFrameSize();
        const double period_ms = config_.period_ms;
        const double buffer_ms = static_cast<double>(buffer_frames) * 1000.0 /
                                 static_cast<double>(config_.sample_rate);
        const double late_threshold_ms = period_ms * 1.5;
        const double callback_warn_ms = period_ms * 0.25;

        uint64_t total_loops = 0;
        uint64_t window_loops = 0;
        uint64_t late_reads = 0;
        uint64_t short_reads = 0;
        uint64_t slow_callbacks = 0;
        uint64_t recover_failures = 0;

        double max_loop_interval_ms = 0.0;
        double max_read_call_ms = 0.0;
        double max_callback_ms = 0.0;

        auto last_loop_start = std::chrono::steady_clock::now();

        std::string s = "Debug capture loop started (tid=" + std::to_string(pthread_self()) +
                        ", period=" + std::to_string(period_ms) +
                        "ms, buffer=" + std::to_string(buffer_ms) +
                        "ms, frame_size=" + std::to_string(frame_size) + " bytes)";
        log(LogLevel::Info, s);

        while (running_)
        {
            ++total_loops;
            ++window_loops;

            auto loop_start = std::chrono::steady_clock::now();
            double loop_interval_ms =
                std::chrono::duration<double, std::milli>(loop_start - last_loop_start).count();
            last_loop_start = loop_start;

            if (loop_interval_ms > max_loop_interval_ms)
                max_loop_interval_ms = loop_interval_ms;

            if (loop_interval_ms > late_threshold_ms)
            {
                ++late_reads;
                s = "[DEBUG] capture loop interval too long: " + std::to_string(loop_interval_ms) +
                    "ms (expected " + std::to_string(period_ms) +
                    "ms, threshold " + std::to_string(late_threshold_ms) + "ms)";
                log(LogLevel::Warn, s);
            }

            auto read_start = std::chrono::steady_clock::now();
            snd_pcm_sframes_t frames = snd_pcm_readi(pcm(), read_buf_.data(), period_frames);
            auto read_end = std::chrono::steady_clock::now();

            double read_call_ms =
                std::chrono::duration<double, std::milli>(read_end - read_start).count();
            if (read_call_ms > max_read_call_ms)
                max_read_call_ms = read_call_ms;

            if (frames < 0)
            {
                s = "[DEBUG] xrun #" + std::to_string(xrunCount() + 1) +
                    ": loop_interval=" + std::to_string(loop_interval_ms) +
                    "ms, read_call=" + std::to_string(read_call_ms) +
                    "ms, err=" + std::to_string(static_cast<int>(frames)) +
                    "(" + snd_strerror(static_cast<int>(frames)) + ")";
                log(LogLevel::Warn, s);

                detail::dumpPcmState(pcm(), "capture-loop-xrun", getLogSink());

                if (!recover(static_cast<int>(frames), "read"))
                {
                    ++recover_failures;
                    std::this_thread::sleep_for(1ms);
                    continue;
                }
                std::this_thread::sleep_for(1ms);
                continue;
            }

            if (static_cast<snd_pcm_uframes_t>(frames) != period_frames)
            {
                ++short_reads;
                s = "[DEBUG] short read: frames=" + std::to_string(static_cast<long>(frames)) +
                    ", expected=" + std::to_string(period_frames) +
                    ", read_call=" + std::to_string(read_call_ms) + "ms";
                log(LogLevel::Warn, s);
            }

            const size_t raw_len = static_cast<size_t>(frames) * frame_size;

            auto callback_start = std::chrono::steady_clock::now();
            raw_cb_(read_buf_.data(), raw_len);
            auto callback_end = std::chrono::steady_clock::now();

            double callback_ms =
                std::chrono::duration<double, std::milli>(callback_end - callback_start).count();
            if (callback_ms > max_callback_ms)
                max_callback_ms = callback_ms;

            if (callback_ms > callback_warn_ms)
            {
                ++slow_callbacks;
                s = "[DEBUG] raw callback is slow: " + std::to_string(callback_ms) +
                    "ms (period " + std::to_string(period_ms) +
                    "ms, warn " + std::to_string(callback_warn_ms) + "ms)";
                log(LogLevel::Warn, s);
            }

            if (window_loops >= 1000)
            {
                s = "[DEBUG] stats: total_loops=" + std::to_string(total_loops) +
                    ", xruns=" + std::to_string(xrunCount()) +
                    ", late_reads=" + std::to_string(late_reads) +
                    ", short_reads=" + std::to_string(short_reads) +
                    ", slow_callbacks=" + std::to_string(slow_callbacks) +
                    ", recover_failures=" + std::to_string(recover_failures) +
                    ", max_loop_interval=" + std::to_string(max_loop_interval_ms) + "ms" +
                    ", max_read_call=" + std::to_string(max_read_call_ms) + "ms" +
                    ", max_callback=" + std::to_string(max_callback_ms) + "ms";
                log(LogLevel::Info, s);

                window_loops = 0;
                max_loop_interval_ms = 0.0;
                max_read_call_ms = 0.0;
                max_callback_ms = 0.0;
            }
        }

        s = "[DEBUG] capture loop ended: total_loops=" + std::to_string(total_loops) +
            ", xruns=" + std::to_string(xrunCount()) +
            ", late_reads=" + std::to_string(late_reads) +
            ", short_reads=" + std::to_string(short_reads) +
            ", slow_callbacks=" + std::to_string(slow_callbacks) +
            ", recover_failures=" + std::to_string(recover_failures);
        log(LogLevel::Info, s);
    }

    void CaptureStreamDebug::onDumpPcmState()
    {
        detail::dumpPcmState(pcm(), "capture-xrun-recover", getLogSink());
    }

} // namespace alsa_rt