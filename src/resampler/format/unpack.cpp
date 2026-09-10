#include "alsa_rt/resampler/format/unpack.hpp"

namespace alsa_rt::unpack
{

    void S24_3LEToS32LE(const uint8_t *src, size_t total_samples, uint8_t *dst)
    {
        for (size_t i = 0; i < total_samples; ++i)
        {
            const uint8_t b0 = src[i * 3 + 0];
            const uint8_t b1 = src[i * 3 + 1];
            const uint8_t b2 = src[i * 3 + 2];
            const uint8_t sign = (b2 & 0x80) ? 0xFF : 0x00;
            dst[i * 4 + 0] = b0;
            dst[i * 4 + 1] = b1;
            dst[i * 4 + 2] = b2;
            dst[i * 4 + 3] = sign;
        }
    }

} // namespace alsa_rt::unpack
