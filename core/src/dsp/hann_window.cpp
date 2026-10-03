#include "dsp/hann_window.h"

#include <cmath>

namespace hro::dsp {

float HannWindow::coefficient(std::size_t index, std::size_t count)
{
    if (count == 0 || index >= count) return 0.0f;
    constexpr double kPi = 3.14159265358979323846;
    const double n_inv = 1.0 / static_cast<double>(count);
    return static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(index) * n_inv));
}

void HannWindow::apply(std::complex<float>* samples, std::size_t count)
{
    if (samples == nullptr || count == 0) return;
    for (std::size_t n = 0; n < count; ++n) samples[n] *= coefficient(n, count);
}

} // namespace hro::dsp
