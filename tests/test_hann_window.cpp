#include "dsp/hann_window.h"

#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

int main()
{
    using hro::dsp::HannWindow;
    using C = std::complex<float>;

    constexpr std::size_t N = 8192;

    std::vector<C> samples(N, C(1.0f, 0.0f));

    HannWindow::apply(samples.data(), samples.size());

    // Periodic Hann:
    //
    // w[n] = 0.5 - 0.5*cos(2*pi*n/N)
    //
    // n = 0       -> 0
    // n = N/4     -> 0.5
    // n = N/2     -> 1
    // n = 3N/4    -> 0.5

    constexpr float eps = 1.0e-6f;

    assert(std::abs(samples[0].real() - 0.0f) < eps);
    assert(std::abs(samples[N / 4].real() - 0.5f) < eps);
    assert(std::abs(samples[N / 2].real() - 1.0f) < eps);
    assert(std::abs(samples[3 * N / 4].real() - 0.5f) < eps);

    // Input was purely real, so imaginary parts remain zero.
    assert(std::abs(samples[N / 2].imag()) < eps);

    // Periodic Hann does not end at exactly zero.
    assert(samples[N - 1].real() > 0.0f);

    // Empty input must be safe.
    HannWindow::apply(nullptr, 0);

    std::cout << "Hann window test passed\n";
    return 0;
}
