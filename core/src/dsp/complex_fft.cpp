#include "dsp/complex_fft.h"

#include <cmath>
#include <utility>

namespace hro::dsp {

namespace {

bool isPowerOfTwo(std::size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

} // namespace

bool ComplexFft::forward(std::complex<float>* samples,
                         std::size_t count)
{
    if (samples == nullptr || !isPowerOfTwo(count)) {
        return false;
    }

    if (count == 1) {
        return true;
    }

    //
    // Bit-reversal permutation
    //
    std::size_t j = 0;

    for (std::size_t i = 1; i < count; ++i) {
        std::size_t bit = count >> 1;

        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }

        j ^= bit;

        if (i < j) {
            std::swap(samples[i], samples[j]);
        }
    }

    //
    // Iterative radix-2 Cooley-Tukey FFT
    //
    constexpr double kPi = 3.14159265358979323846;

    for (std::size_t len = 2; len <= count; len <<= 1) {

        const double angle =
            -2.0 * kPi / static_cast<double>(len);

        const std::complex<float> wlen(
            static_cast<float>(std::cos(angle)),
            static_cast<float>(std::sin(angle)));

        for (std::size_t i = 0; i < count; i += len) {

            std::complex<float> w(1.0f, 0.0f);

            const std::size_t half = len >> 1;

            for (std::size_t k = 0; k < half; ++k) {

                const std::complex<float> u =
                    samples[i + k];

                const std::complex<float> v =
                    samples[i + k + half] * w;

                samples[i + k] = u + v;
                samples[i + k + half] = u - v;

                w *= wlen;
            }
        }
    }

    return true;
}

} // namespace hro::dsp

