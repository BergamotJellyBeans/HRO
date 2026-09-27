#include "dsp/complex_fft.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {

using C = std::complex<float>;

constexpr std::size_t N = 8192;
constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 8192.0;

std::vector<C> makeTone(double frequency_hz)
{
    std::vector<C> samples(N);

    for (std::size_t n = 0; n < N; ++n) {
        const double phase =
            2.0 * kPi * frequency_hz *
            static_cast<double>(n) / kSampleRate;

        samples[n] = C(
            static_cast<float>(std::cos(phase)),
            static_cast<float>(std::sin(phase)));
    }

    return samples;
}

std::size_t findPeakBin(const std::vector<C>& spectrum)
{
    std::size_t peak_bin = 0;
    float peak_power = -1.0f;

    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const float power = std::norm(spectrum[k]);

        if (power > peak_power) {
            peak_power = power;
            peak_bin = k;
        }
    }

    return peak_bin;
}

void testTone(double frequency_hz,
              std::size_t expected_bin)
{
    auto samples = makeTone(frequency_hz);

    const bool ok =
        hro::dsp::ComplexFft::forward(
            samples.data(), samples.size());

    assert(ok);

    const std::size_t peak_bin =
        findPeakBin(samples);

    std::cout
        << "Frequency " << frequency_hz
        << " Hz -> peak bin " << peak_bin
        << " (expected " << expected_bin << ")\n";

    assert(peak_bin == expected_bin);
}

} // namespace

int main()
{
    //
    // Fs = 8192 Hz, N = 8192
    //
    // Therefore:
    //
    //     1 bin = 1 Hz
    //

    testTone(0.0,   0);
    testTone(1.0,   1);
    testTone(780.0, 780);

    // Negative frequencies appear at the upper end
    // of the complex FFT spectrum.
    testTone(-1.0, N - 1);

    //
    // Invalid input checks.
    //
    assert(!hro::dsp::ComplexFft::forward(nullptr, N));

    C invalid[3] = {};
    assert(!hro::dsp::ComplexFft::forward(invalid, 3));

    //
    // N = 1 is a valid FFT and must remain unchanged.
    //
    C single(2.0f, -3.0f);

    assert(hro::dsp::ComplexFft::forward(&single, 1));
    assert(single == C(2.0f, -3.0f));

    std::cout << "Complex FFT test passed\n";

    return 0;
}

