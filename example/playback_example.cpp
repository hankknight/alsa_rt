// playback_example.cpp
//
// Quick-start: synthesize a sine tone and play it through an ALSA device.
//
// What it shows:
//   - Building an AlsaConfig for playback.
//   - Creating a PlayBackStream, setting the ring buffer length and a LogSink.
//   - Feeding interleaved PCM with feed() while the library drains the ring
//     on its own real-time thread (padding silence when the ring runs dry).
//
// Usage:
//   ./playback_example [device] [freq]    defaults: "default" 440.0

#include "alsa_rt/device/alsa_config.hpp"
#include "alsa_rt/playback/playback_stream.hpp"
#include "alsa_rt/utils/logging.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

using alsa_rt::AlsaConfig;
using alsa_rt::LogLevel;
using alsa_rt::PlayBackStream;

static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop.store(true); }

int main(int argc, char **argv)
{
    const std::string device = (argc > 1) ? argv[1] : "default";

    double freq = 440.0; // A4
    try
    {
        if (argc > 2) freq = std::stod(argv[2]);
    }
    catch (const std::exception &)
    {
        std::cerr << "invalid frequency argument\n";
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // 16-bit / 48 kHz / stereo / 10 ms period.
    AlsaConfig cfg(device, 2, 48000, SND_PCM_FORMAT_S16_LE, 10.0);

    PlayBackStream pb(cfg);
    pb.setLogSink([](LogLevel lvl, std::string_view msg) {
        std::cerr << "[pb] " << msg << '\n';
    });

    // Keep roughly 1 s of audio in the SPSC ring. The ring cushions timing
    // jitter between this producer thread and the library's consumer thread.
    pb.setBufferSeconds(1);

    if (!pb.start())
    {
        std::cerr << "failed to start playback\n";
        return 1;
    }

    const uint32_t rate = cfg.sample_rate;
    const uint32_t period_frames = static_cast<uint32_t>(cfg.getPeriodFrames());
    const uint32_t frame_size = cfg.getFrameSize(); // 4 bytes: 2 ch * s16

    // One period worth of interleaved stereo samples, regenerated each cycle.
    std::vector<uint8_t> period(period_frames * frame_size);

    constexpr double kTwoPi = 6.28318530717958647692;
    const double amp = 0.2; // keep amplitude modest to avoid clipping/loudness
    const double phase_inc = kTwoPi * freq / rate;
    double phase = 0.0;

    // Pace the producer at roughly real-time (one period per period). feed() is
    // non-blocking: if the ring were ever full it would discard the oldest data
    // to keep the newest, so feeding slightly fast is also safe.
    const auto period_ms = std::chrono::milliseconds(period_frames * 1000 / rate);

    std::cerr << "playing " << freq << " Hz on '" << device << "' ... Ctrl+C to stop\n";

    while (!g_stop.load())
    {
        auto *samples = reinterpret_cast<int16_t *>(period.data());
        for (uint32_t i = 0; i < period_frames; ++i)
        {
            const int16_t v = static_cast<int16_t>(amp * std::sin(phase) * 32767.0);
            samples[i * 2 + 0] = v; // left
            samples[i * 2 + 1] = v; // right
            phase += phase_inc;
            if (phase > kTwoPi)
                phase -= kTwoPi;
        }

        pb.feed(period);
        std::this_thread::sleep_for(period_ms);
    }

    pb.stop();
    std::cerr << "stopped.\n";
    return 0;
}

