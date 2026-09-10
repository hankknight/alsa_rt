#pragma once

#include <cstdint>
#include <cstddef>

namespace alsa_rt::unpack
{

    void S24_3LEToS32LE(const uint8_t *src, size_t total_samples, uint8_t *dst);

} // namespace alsa_rt::unpack
