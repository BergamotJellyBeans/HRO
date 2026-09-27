#pragma once

#include <complex>
#include <cstddef>

namespace hro::dsp {

class NcoShifter
{
public:
    explicit NcoShifter(double sample_rate_hz);

    void setFrequencyShift(double frequency_hz);
    void reset();

    void process(std::complex<float>* samples, std::size_t count);

    double sampleRateHz() const { return sample_rate_hz_; }
    double frequencyShiftHz() const { return frequency_hz_; }

private:
    void updateStep();

    double sample_rate_hz_;
    double frequency_hz_ = 0.0;

    std::complex<double> oscillator_{1.0, 0.0};
    std::complex<double> step_{1.0, 0.0};

    std::size_t samples_since_normalize_ = 0;
};

} // namespace hro::dsp
