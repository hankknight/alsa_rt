// playback_priority_example.cpp
//
// Quick-start: priority-based playback preemption.
//
// A low-priority background tone (priority 0) plays continuously. After 3
// seconds a higher-priority beep (priority 5) is fed; PlayBackPriorityStream
// preempts the background audio by discarding the buffered low-priority data
// so the beep starts almost immediately. When the beep stream stops feeding,
// the ring drains and priority resets to 0.
//
// What it shows:
//   - PlayBackPriorityStream and the feed(priority, data) API.
//   - How a higher priority value interrupts lower-priority buffered audio.
//
// Usage:
//   ./playback_priority_example [device]    default: "default"

#include "alsa_rt/device/alsa_config.hpp"
#include "alsa_rt/playback/playback_stream.hpp" // declares PlayBackPriorityStream
#include "alsa_rt/utils/logging.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

using alsa_rt::AlsaConfig;
using alsa_rt::LogLevel;
using alsa_rt::PlayBackPriorityStream;

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop.store(true); }

// Fill `buf` with `frames` samples of a sine tone, interleaved stereo S16.
static void fillSine(std::vector<uint8_t> &buf, uint32_t frames, uint32_t rate,
                     double freq, double amp, double &phase)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    auto *s = reinterpret_cast<int16_t *>(buf.data());
    const double inc = kTwoPi * freq / rate;
    for (uint32_t i = 0; i < frames; ++i)
    {
        const int16_t v = static_cast<int16_t>(amp * std::sin(phase) * 32767.0);
        s[i * 2 + 0] = v; // left
        s[i * 2 + 1] = v; // right
        phase += inc;
        if (phase > kTwoPi)
            phase -= kTwoPi;
    }
}

int main(int argc, char **argv)
{
    const std::string device = (argc > 1) ? argv[1] : "default";

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    AlsaConfig cfg(device, 2, 48000, SND_PCM_FORMAT_S16_LE, 10.0);

    PlayBackPriorityStream pb(cfg);
    pb.setLogSink([](LogLevel lvl, std::string_view msg) {
        std::cerr << "[pbp] " << msg << '\n';
    });
    pb.setBufferSeconds(1);

    if (!pb.start())
    {
        std::cerr << "failed to start playback\n";
        return 1;
    }

    const uint32_t rate = cfg.sample_rate;
    const uint32_t period_frames = static_cast<uint32_t>(cfg.getPeriodFrames());
    const uint32_t frame_size = cfg.getFrameSize();
    const auto period_ms = std::chrono::milliseconds(period_frames * 1000 / rate);

    std::vector<uint8_t> period(period_frames * frame_size);
    double phase = 0.0;

    // Phase 1: background tone at priority 0 for 3 seconds.
    std::cerr << "background tone 440 Hz (priority 0) for 3 s ...\n";
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!g_stop.load() && std::chrono::steady_clock::now() < deadline)
    {
        fillSine(period, period_frames, rate, 440.0, 0.2, phase);
        pb.feed(0, period); // lowest priority
        std::this_thread::sleep_for(period_ms);
    }

    // Phase 2: preempt with a higher-priority beep for 2 seconds. The library
    // logs a "Priority preemption" line and skips the consumer read position to
    // the latest write position, dropping the buffered 440 Hz audio.
    std::cerr << "preempt with 880 Hz beep (priority 5) for 2 s ...\n";
    phase = 0.0;
    deadline = std::chrono::steady_clock::now() + 2s;
    while (!g_stop.load() && std::chrono::steady_clock::now() < deadline)
    {
        fillSine(period, period_frames, rate, 880.0, 0.25, phase);
        pb.feed(5, period); // preempts priority 0
        std::this_thread::sleep_for(period_ms);
    }

    // After we stop feeding, the ring drains; the debug stats reset priority to
    // 0 on starvation. Keep the process alive briefly so the tail is audible.
    std::cerr << "done. Ctrl+C to exit.\n";
    while (!g_stop.load())
        std::this_thread::sleep_for(100ms);

    pb.stop();
    return 0;
}

