#pragma once

#include "alsa_rt/playback/playback_stream.hpp"

namespace alsa_rt
{

    class PlayBackStreamDebug : public PlayBackStream
    {
    public:
        explicit PlayBackStreamDebug(AlsaConfig cfg);

    protected:
        void loop() override;

        void onDumpPcmState() override;
    };

    class PlayBackPriorityStreamDebug : public PlayBackPriorityStream
    {
    public:
        explicit PlayBackPriorityStreamDebug(AlsaConfig cfg);

    protected:
        void loop() override;

        void onDumpPcmState() override;
    };

} // namespace alsa_rt