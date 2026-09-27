#pragma once

#include <complex>
#include <cstddef>

namespace hro::dsp {

class HannWindow {
public:
    // Apply a periodic Hann window:
    //
    // w[n] = 0.5 - 0.5*cos(2*pi*n/N)
    //
    // Intended for FFT analysis.
    static void apply(std::complex<float>* samples, std::size_t count);
};

} // namespace hro::dsp

