#pragma once

#include "alsa_rt/capture/capture_stream.hpp"

namespace alsa_rt
{

    class CaptureStreamDebug : public CaptureStream
    {
    public:
        explicit CaptureStreamDebug(AlsaConfig cfg);

    protected:
        void loop() override;

        void onDumpPcmState() override;
    };

} // namespace alsa_rt