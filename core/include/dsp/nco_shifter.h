#pragma once

#include <complex>
#include <cstddef>
#include <cmath>

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

// Single-precision oscillator for processors with a float-only hardware FPU.
// Same frequency-shift convention as NcoShifter; state is continuous across blocks.
class FloatNcoShifter {
public:
    explicit FloatNcoShifter(float sample_rate_hz) : sample_rate_hz_(sample_rate_hz) {}
    void setFrequencyShift(float frequency_hz) {
        const double phase = 6.2831853071795864769 * frequency_hz / sample_rate_hz_;
        step_ = {static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase))};
    }
    void reset() { oscillator_ = {1.0f, 0.0f}; samples_since_normalize_ = 0; }
    void process(std::complex<float>* samples, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            samples[i] *= oscillator_;
            oscillator_ *= step_;
            if (++samples_since_normalize_ >= 4096) {
                const float magnitude = std::abs(oscillator_);
                if (magnitude > 0.0f) oscillator_ /= magnitude;
                samples_since_normalize_ = 0;
            }
        }
    }
private:
    float sample_rate_hz_;
    std::complex<float> oscillator_{1.0f, 0.0f};
    std::complex<float> step_{1.0f, 0.0f};
    std::size_t samples_since_normalize_ = 0;
};

} // namespace hro::dsp
