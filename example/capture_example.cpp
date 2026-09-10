// capture_example.cpp
//
// Quick-start: capture PCM from an ALSA device and write the raw samples to a
// file so you can verify real audio was recorded (not just watch byte counters
// grow). No container/WAV header is written -- just the bare interleaved PCM.
//
// Usage:
//   ./capture_example [device] [sample_rate] [channels] [seconds]
//   defaults: "default" 48000 2 5
//
// Output: ./capture.pcm  (raw 16-bit signed LE, interleaved)
//   Play it back to verify (adjust -r/-c to the arguments you passed):
//     aplay -t raw -f S16_LE -r 48000 -c 2 capture.pcm
//
// Tip on device strings:
//   - "plughw:C,D" : ALSA plugin layer. Converts format/rate/channels
//     automatically, so almost any parameter combination works.
//   - "hw:C,D"     : direct hardware. Requires the card to natively support
//     the requested rate/channels/format, otherwise open fails.
//   - "default"    : usually routes through plughw/pulse; widely compatible.

#include "alsa_rt/capture/capture_stream.hpp"
#include "alsa_rt/device/alsa_config.hpp"
#include "alsa_rt/utils/logging.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

using alsa_rt::AlsaConfig;
using alsa_rt::CaptureStream;
using alsa_rt::LogLevel;

// Stop flag toggled by the signal handler. The capture loop runs on its own
// thread inside the library; we only need to signal the main thread here.
// std::signal is the C++ standard-library signal facility (<csignal>); there
// is no higher-level C++ replacement for it.
static std::atomic<bool> g_stop{false};
static void onSignal(int) { g_stop.store(true); }

int main(int argc, char **argv)
{
    const std::string device = (argc > 1) ? argv[1] : "default";

    // Parse numeric arguments with std::stoul (throws on bad input) instead of
    // C's atoi, so malformed values are reported rather than silently zeroed.
    uint32_t sample_rate = 48000;
    uint32_t channels = 2;
    uint32_t seconds = 5;
    try
    {
        if (argc > 2) sample_rate = std::stoul(argv[2]);
        if (argc > 3) channels = std::stoul(argv[3]);
        if (argc > 4) seconds = std::stoul(argv[4]);
    }
    catch (const std::exception &)
    {
        std::cerr << "invalid numeric argument\n";
        return 1;
    }

    if (channels == 0 || sample_rate == 0 || seconds == 0)
    {
        std::cerr << "invalid arguments (rate, channels and seconds must be > 0)\n";
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Fixed at 16-bit signed LE to keep the raw output simple. If your card
    // does not support S16_LE natively, use a "plughw" device which converts.
    AlsaConfig cfg(device, channels, sample_rate, SND_PCM_FORMAT_S16_LE, 10.0);

    CaptureStream cap(cfg);
    cap.setLogSink([](LogLevel lvl, std::string_view msg) {
        std::cerr << "[cap] " << msg << '\n';
    });

    // Pre-allocate the capture buffer for the whole clip. The callback runs on
    // the library's real-time thread; the main thread only touches pcm_buf
    // after cap.stop() joins that thread, so no lock is needed.
    const size_t bytes_per_sec = static_cast<size_t>(cfg.getByteRate());
    std::vector<uint8_t> pcm_buf;
    pcm_buf.reserve(static_cast<size_t>(seconds) * bytes_per_sec);

    uint64_t total_frames = 0;

    // The callback fires once per period with interleaved PCM samples. We just
    // accumulate them; a real app would resample / encode / ship them here.
    cap.setRawPcmCallback([&](const uint8_t *data, size_t len) {
        pcm_buf.insert(pcm_buf.end(), data, data + len);
        total_frames += len / cfg.getFrameSize();
    });

    if (!cap.start())
    {
        std::cerr << "failed to start capture\n";
        return 1;
    }

    std::cerr << "capturing " << seconds << " s from '" << device
              << "' (" << sample_rate << " Hz, " << channels
              << " ch, S16LE) ... Ctrl+C to stop early\n";

    // Wait for the requested duration or an early stop signal.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!g_stop.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(100ms);

    // stop() clears the internal run flag, joins the capture thread and closes
    // the PCM device. Safe to call from the main thread.
    cap.stop();

    // Write the raw samples. ostream::write requires a const char*, so a
    // reinterpret_cast is unavoidable here -- this is the standard C++ way to
    // do binary I/O of byte buffers.
    const std::string out_path = "capture.pcm";
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        std::cerr << "failed to open " << out_path << '\n';
        return 1;
    }
    out.write(reinterpret_cast<const char *>(pcm_buf.data()),
              static_cast<std::streamsize>(pcm_buf.size()));
    out.close();

    if (!out)
    {
        std::cerr << "failed to write " << out_path << '\n';
        return 1;
    }

    std::cerr << "done. wrote " << out_path << " (" << pcm_buf.size()
              << " bytes, " << total_frames << " frames)\n";
    std::cerr << "play with: aplay -t raw -f S16_LE -r " << sample_rate
              << " -c " << channels << ' ' << out_path << '\n';
    return 0;
}

