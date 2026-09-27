#include "dsp/nco_shifter.h"

#include <cmath>

namespace hro::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr std::size_t kNormalizeInterval = 4096;
}

NcoShifter::NcoShifter(double sample_rate_hz)
    : sample_rate_hz_(sample_rate_hz)
{
    updateStep();
}

void NcoShifter::setFrequencyShift(double frequency_hz)
{
    frequency_hz_ = frequency_hz;
    updateStep();
}

void NcoShifter::reset()
{
    oscillator_ = {1.0, 0.0};
    samples_since_normalize_ = 0;
}

void NcoShifter::updateStep()
{
    const double phase_step =
        kTwoPi * frequency_hz_ / sample_rate_hz_;

    step_ = {
        std::cos(phase_step),
        std::sin(phase_step)
    };
}

void NcoShifter::process(std::complex<float>* samples,
                         std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        const std::complex<double> input{
            static_cast<double>(samples[i].real()),
            static_cast<double>(samples[i].imag())
        };

        const std::complex<double> output = input * oscillator_;

        samples[i] = {
            static_cast<float>(output.real()),
            static_cast<float>(output.imag())
        };

        oscillator_ *= step_;

        ++samples_since_normalize_;

        if (samples_since_normalize_ >= kNormalizeInterval) {
            const double magnitude = std::abs(oscillator_);

            if (magnitude > 0.0) {
                oscillator_ /= magnitude;
            }

            samples_since_normalize_ = 0;
        }
    }
}

} // namespace hro::dsp
