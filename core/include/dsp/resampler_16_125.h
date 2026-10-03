#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <vector>

namespace hro::dsp {

class Resampler16_125
{
public:
    static constexpr std::size_t kInterpolation = 16;
    static constexpr std::size_t kDecimation = 125;
    static constexpr std::size_t kNumTaps = 2464;
    static constexpr std::size_t kTapsPerPhase =
        kNumTaps / kInterpolation;

    Resampler16_125();

    void reset();

    // Allocation-free streaming API for embedded callers.
    bool processOne(std::complex<float> input, std::complex<float>& output);

    void process(
        const std::complex<float>* input,
        std::size_t count,
        std::vector<std::complex<float>>& output);

private:
    using Complex = std::complex<float>;

    // Input-rate delay line.
    // Each polyphase branch uses 154 input samples.
    std::array<Complex, kTapsPerPhase> delay_{};

    std::size_t write_pos_ = 0;

    // Rational-resampler time accumulator.
    //
    // Units are 1/16 of one input sample.
    // The accumulator advances by 125 for every output sample.
    std::size_t time_accumulator_ = 0;
};

} // namespace hro::dsp
