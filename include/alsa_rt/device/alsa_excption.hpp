#pragma once

#include <string>
#include <alsa/asoundlib.h>

#include "alsa_rt/utils/logging.hpp"

namespace alsa_rt::detail
{

    std::string formatXrun(int32_t err_code, const std::string &source, uint64_t total);

    std::string formatOpenInfo(uint32_t rate, uint32_t channels,
                               snd_pcm_format_t format,
                               snd_pcm_uframes_t period, snd_pcm_uframes_t buffer);

    void dumpPcmState(snd_pcm_t *pcm, const std::string &tag, const LogSink &log_sink);

} // namespace alsa_rt::detail
