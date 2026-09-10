#include <chrono>

#include "alsa_rt/playback/playback_stream_debug.hpp"

using namespace std::chrono_literals;

namespace alsa_rt
{

    PlayBackStreamDebug::PlayBackStreamDebug(AlsaConfig cfg) : PlayBackStream(cfg) {}

    void PlayBackStreamDebug::loop()
    {
        const auto period_frames = static_cast<int>(config_.getPeriodFrames());
        const auto buffer_frames = config_.getBufferFrames();
        const auto period_bytes = static_cast<size_t>(config_.getPeriodBytes());
        const double period_ms = static_cast<double>(config_.getPeriodFrames()) * 1000.0 /
                                 static_cast<double>(config_.sample_rate);
        const double buffer_ms = static_cast<double>(buffer_frames) * 1000.0 /
                                 static_cast<double>(config_.sample_rate);
        const double late_threshold_ms = period_ms * 1.5;

        uint64_t total_loops = 0;
        uint64_t window_loops = 0;
        uint64_t late_writes = 0;
        uint64_t underruns = 0;
        uint64_t starvations = 0;
        uint64_t partial_periods = 0;
        uint64_t recover_failures = 0;
        uint64_t priority_flushes = 0;

        double max_loop_interval_ms = 0.0;
        double max_write_call_ms = 0.0;
        double max_ring_usage_pct = 0.0;
        size_t min_available_bytes = data_ring_ ? data_ring_->capacity() : 0;
        size_t max_available_bytes = 0;

        auto last_loop_start = std::chrono::steady_clock::now();

        std::string s = "Debug write loop started (tid=" + std::to_string(pthread_self()) +
                        ", period=" + std::to_string(period_ms) +
                        "ms, buffer=" + std::to_string(buffer_ms) +
                        "ms, period_bytes=" + std::to_string(period_bytes) +
                        ", ring_capacity=" + std::to_string(data_ring_ ? data_ring_->capacity() : 0) +
                        " bytes)";
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
                ++late_writes;
                s = "[DEBUG] write loop interval too long: " + std::to_string(loop_interval_ms) +
                    "ms (expected " + std::to_string(period_ms) +
                    "ms, threshold " + std::to_string(late_threshold_ms) + "ms)";
                log(LogLevel::Warn, s);
            }

            size_t available = data_ring_->popAvailable();
            size_t ring_capacity = data_ring_->capacity();
            size_t to_read = std::min(available, period_bytes);
            double ring_usage_pct = ring_capacity > 0
                                        ? static_cast<double>(available) * 100.0 / static_cast<double>(ring_capacity)
                                        : 0.0;

            if (available < min_available_bytes)
                min_available_bytes = available;
            if (available > max_available_bytes)
                max_available_bytes = available;
            if (ring_usage_pct > max_ring_usage_pct)
                max_ring_usage_pct = ring_usage_pct;

            if (to_read > 0)
                data_ring_->pop(write_buffer_.data(), to_read);

            if (to_read < period_bytes)
            {
                std::fill(write_buffer_.begin() + to_read, write_buffer_.end(), 0);

                if (to_read == 0)
                {
                    ++starvations;

                    if (starvations % 100 == 1)
                    {
                        s = "[DEBUG] ring starvation #" + std::to_string(starvations) +
                            ": available=0, ring_capacity=" + std::to_string(ring_capacity) +
                            ", current_priority reset to 0";
                        log(LogLevel::Warn, s);
                    }
                }
                else
                {
                    ++partial_periods;
                    s = "[DEBUG] partial period: read=" + std::to_string(to_read) +
                        "/" + std::to_string(period_bytes) +
                        " bytes, padding silence=" + std::to_string(period_bytes - to_read) +
                        " bytes, ring_usage=" + std::to_string(ring_usage_pct) + "%";
                    log(LogLevel::Warn, s);
                }
            }

            auto write_start = std::chrono::steady_clock::now();
            int32_t err = writeFrames(write_buffer_.data(), period_frames);
            auto write_end = std::chrono::steady_clock::now();

            double write_call_ms =
                std::chrono::duration<double, std::milli>(write_end - write_start).count();
            if (write_call_ms > max_write_call_ms)
                max_write_call_ms = write_call_ms;

            if (write_call_ms > late_threshold_ms)
            {
                s = "[DEBUG] write call too long: " + std::to_string(write_call_ms) +
                    "ms (period " + std::to_string(period_ms) +
                    "ms), ring_available=" + std::to_string(available) +
                    "/" + std::to_string(ring_capacity) +
                    " bytes(" + std::to_string(ring_usage_pct) + "%)";
                log(LogLevel::Warn, s);
            }

            if (err < 0)
            {
                ++underruns;
                s = "[DEBUG] xrun #" + std::to_string(xrunCount() + 1) +
                    ": loop_interval=" + std::to_string(loop_interval_ms) +
                    "ms, write_call=" + std::to_string(write_call_ms) +
                    "ms, ring_available=" + std::to_string(available) +
                    "/" + std::to_string(ring_capacity) +
                    " bytes(" + std::to_string(ring_usage_pct) + "%), err=" +
                    std::to_string(err) + "(" + snd_strerror(err) + ")";
                log(LogLevel::Warn, s);

                detail::dumpPcmState(pcm(), "play-loop-xrun", getLogSink());

                if (!recover(err, "write"))
                {
                    ++recover_failures;
                    std::this_thread::sleep_for(1ms);
                    continue;
                }
                std::this_thread::sleep_for(1ms);
                continue;
            }

            if (window_loops >= 1000)
            {
                s = "[DEBUG] stats: total_loops=" + std::to_string(total_loops) +
                    ", xruns=" + std::to_string(xrunCount()) +
                    ", underruns=" + std::to_string(underruns) +
                    ", starvations=" + std::to_string(starvations) +
                    ", partial_periods=" + std::to_string(partial_periods) +
                    ", late_writes=" + std::to_string(late_writes) +
                    ", priority_flushes=" + std::to_string(priority_flushes) +
                    ", recover_failures=" + std::to_string(recover_failures) +
                    ", min_available=" + std::to_string(min_available_bytes) + " bytes" +
                    ", max_available=" + std::to_string(max_available_bytes) + " bytes" +
                    ", max_ring_usage=" + std::to_string(max_ring_usage_pct) + "%" +
                    ", max_loop_interval=" + std::to_string(max_loop_interval_ms) + "ms" +
                    ", max_write_call=" + std::to_string(max_write_call_ms) + "ms";
                log(LogLevel::Info, s);

                window_loops = 0;
                max_loop_interval_ms = 0.0;
                max_write_call_ms = 0.0;
                max_ring_usage_pct = 0.0;
                min_available_bytes = ring_capacity;
                max_available_bytes = 0;
            }
        }

        s = "[DEBUG] write loop ended: total_loops=" + std::to_string(total_loops) +
            ", xruns=" + std::to_string(xrunCount()) +
            ", underruns=" + std::to_string(underruns) +
            ", starvations=" + std::to_string(starvations) +
            ", partial_periods=" + std::to_string(partial_periods) +
            ", late_writes=" + std::to_string(late_writes) +
            ", priority_flushes=" + std::to_string(priority_flushes) +
            ", recover_failures=" + std::to_string(recover_failures);
        log(LogLevel::Info, s);
    }

    void PlayBackStreamDebug::onDumpPcmState()
    {
        detail::dumpPcmState(pcm(), "xrun-recover", getLogSink());
    }

} // namespace alsa_rt
