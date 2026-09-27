#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace hro::dsp {

/**
 * Complex FIR decimator:
 *
 *   960,000 complex samples/s
 *          ↓
 *   255-tap real FIR
 *          ↓
 *       decimate /15
 *          ↓
 *    64,000 complex samples/s
 *
 * FIR design:
 *   Passband       : +/- 10 kHz
 *   Stopband start : +/- 32 kHz
 *   Kaiser beta    : 7.85726
 *   255 taps
 *
 * Filter state is preserved across process() calls.
 */
class Decimator15
{
public:
    static constexpr std::size_t kDecimation = 15;
    static constexpr std::size_t kNumTaps = 255;

    Decimator15();

    void reset();

    /**
     * Process a block of complex input samples.
     *
     * Output samples are appended to 'output'.
     * State and decimation phase are preserved between blocks.
     */
    void process(
        const std::complex<float>* input,
        std::size_t count,
        std::vector<std::complex<float>>& output);

private:
    std::array<std::complex<float>, kNumTaps> delay_{};

    std::size_t write_pos_ = 0;
    std::size_t decimation_phase_ = 0;
};

} // namespace hro::dsp
