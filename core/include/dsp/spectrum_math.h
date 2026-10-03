#pragma once
#include <cmath>
#include <cstdint>
namespace hro::dsp {
inline float normalizeCu8(std::uint8_t value) {
    return (static_cast<float>(value) - 127.5f) / 127.5f;
}
inline float powerDb(float power) {
    return 10.0f * std::log10(power + 1.0e-20f);
}
}
