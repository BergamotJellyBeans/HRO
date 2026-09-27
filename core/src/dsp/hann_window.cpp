#include "dsp/hann_window.h"

#include <cmath>

namespace hro::dsp {

void HannWindow::apply(std::complex<float>* samples, std::size_t count)
{
    if (samples == nullptr || count == 0) {
        return;
    }

    constexpr double kPi = 3.14159265358979323846;

    const double n_inv = 1.0 / static_cast<double>(count);

    for (std::size_t n = 0; n < count; ++n) {
        const double phase =
            2.0 * kPi * static_cast<double>(n) * n_inv;

        const float w =
            static_cast<float>(0.5 - 0.5 * std::cos(phase));

        samples[n] *= w;
    }
}

} // namespace hro::dsp
