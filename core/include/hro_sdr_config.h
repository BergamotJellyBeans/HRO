#pragma once

#include <array>

namespace hro
{

inline constexpr std::array<int, 29> SDR_GAIN_VALUES = {
      0,   9,  14,  27,  37,
     77,  87, 125, 144, 157,
    166, 197, 207, 229, 254,
    280, 297, 328, 338, 364,
    372, 386, 402, 421, 434,
    439, 445, 480, 496
};

inline constexpr int DEFAULT_SDR_GAIN = 402;

} // namespace hro
