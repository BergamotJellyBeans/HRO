#pragma once

#include <complex>
#include <cstddef>

namespace hro::dsp {

class ComplexFft {
public:
    // In-place forward complex FFT.
    //
    // Definition:
    //
    // X[k] = sum(x[n] * exp(-j*2*pi*k*n/N))
    //
    // count must be a power of two.
    //
    // No amplitude normalization is performed.
    //
    // Returns false if the input is invalid.
    static bool forward(std::complex<float>* samples,
                        std::size_t count);
};

} // namespace hro::dsp
