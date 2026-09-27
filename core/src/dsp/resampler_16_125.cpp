#include "dsp/resampler_16_125.h"
#include "dsp/resampler_16_125_taps.h"

namespace hro::dsp {

Resampler16_125::Resampler16_125()
{
    reset();
}

void Resampler16_125::reset()
{
    delay_.fill({0.0f, 0.0f});
    write_pos_ = 0;
    time_accumulator_ = 0;
}

void Resampler16_125::process(
    const std::complex<float>* input,
    std::size_t count,
    std::vector<std::complex<float>>& output)
{
    std::size_t input_index = 0;

    while (input_index < count) {

        // Store one new input sample.
        delay_[write_pos_] = input[input_index];

        ++write_pos_;
        if (write_pos_ == kTapsPerPhase) {
            write_pos_ = 0;
        }

        ++input_index;

        // One input sample corresponds to 16 units
        // in the interpolation-rate time base.
        time_accumulator_ += kInterpolation;

        // Generate every output sample whose time has now arrived.
        while (time_accumulator_ >= kDecimation) {

            time_accumulator_ -= kDecimation;

            const std::size_t phase =
                kInterpolation - 1 - time_accumulator_;

            Complex sum{0.0f, 0.0f};

            std::size_t pos = write_pos_;

            for (std::size_t k = 0; k < kTapsPerPhase; ++k) {

                if (pos == 0) {
                    pos = kTapsPerPhase - 1;
                } else {
                    --pos;
                }

                const std::size_t tap_index =
                    phase + k * kInterpolation;

                sum += delay_[pos] *
                       kResampler16_125Taps[tap_index];
            }

            // firwin coefficients have unity DC gain at the
            // interpolation-rate representation. Compensate
            // for zero insertion by the interpolation factor.
            output.push_back(
                sum * static_cast<float>(kInterpolation)
            );
        }
    }
}

} // namespace hro::dsp

