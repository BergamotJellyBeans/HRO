#include "dsp/decimator_15.h"
#include "dsp/decimator_15_taps.h"

#include <algorithm>

namespace hro::dsp {

Decimator15::Decimator15()
{
    reset();
}

void Decimator15::reset()
{
    delay_.fill({0.0f, 0.0f});
    write_pos_ = 0;
    decimation_phase_ = 0;
}

void Decimator15::process(
    const std::complex<float>* input,
    std::size_t count,
    std::vector<std::complex<float>>& output)
{
    for (std::size_t n = 0; n < count; ++n) {

        // Store newest input sample in circular delay line.
        delay_[write_pos_] = input[n];

        // Produce one output for every 15 input samples.
        if (decimation_phase_ == 0) {

            std::complex<float> sum{0.0f, 0.0f};

            // kDecimator15Taps[0] multiplies the newest sample.
            std::size_t pos = write_pos_;

            for (std::size_t k = 0; k < kNumTaps; ++k) {
                sum += delay_[pos] * kDecimator15Taps[k];

                if (pos == 0) {
                    pos = kNumTaps - 1;
                } else {
                    --pos;
                }
            }

            output.push_back(sum);
        }

        ++write_pos_;
        if (write_pos_ == kNumTaps) {
            write_pos_ = 0;
        }

        ++decimation_phase_;
        if (decimation_phase_ == kDecimation) {
            decimation_phase_ = 0;
        }
    }
}

} // namespace hro::dsp
