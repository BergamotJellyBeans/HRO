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

bool Resampler16_125::processOne(Complex input, Complex& output)
{
    delay_[write_pos_] = input;
    write_pos_ = (write_pos_ + 1) % kTapsPerPhase;
    time_accumulator_ += kInterpolation;
    if (time_accumulator_ < kDecimation) return false;
    time_accumulator_ -= kDecimation;
    const std::size_t phase = kInterpolation - 1 - time_accumulator_;
    Complex sum{0.0f, 0.0f};
    std::size_t pos = write_pos_;
    for (std::size_t k = 0; k < kTapsPerPhase; ++k) {
        pos = pos == 0 ? kTapsPerPhase - 1 : pos - 1;
        sum += delay_[pos] * kResampler16_125Taps[phase + k * kInterpolation];
    }
    output = sum * static_cast<float>(kInterpolation);
    return true;
}

void Resampler16_125::process(const Complex* input, std::size_t count,
                             std::vector<Complex>& output)
{
    for (std::size_t i = 0; i < count; ++i) {
        Complex sample;
        if (processOne(input[i], sample)) output.push_back(sample);
    }
}

} // namespace hro::dsp
