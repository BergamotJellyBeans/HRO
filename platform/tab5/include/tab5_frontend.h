#pragma once
#include <array>
#include <complex>
#include <cstdint>
#include "dsp/fs4_rotator.h"
namespace hro::tab5 {
inline constexpr std::uint32_t INPUT_RATE = 256000;
inline constexpr std::uint32_t OUTPUT_RATE = 64000;
// Tab5固有の+Fs/4回転と127-tap FIR /4。状態はUSBブロック間で保持。
class Frontend {
public:
    bool process(float i, float q, std::complex<float>& output, bool rotate = true);
private:
    std::array<std::complex<float>, 127> delay_{};
    hro::dsp::Fs4Rotator rotator_;
    unsigned decimation_ = 0;
    unsigned position_ = 0;
};
}
