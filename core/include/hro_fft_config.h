#pragma once

#include <cstddef>

namespace hro {

constexpr int FFT_RANGE_HZ = 300;

constexpr std::size_t FFT_BIN_COUNT =
    static_cast<std::size_t>(FFT_RANGE_HZ * 2 + 1);

} // namespace hro
