#include "dsp/nco_shifter.h"

#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

double estimateFrequency(const std::vector<std::complex<float>>& x,
                         double sample_rate_hz)
{
    std::complex<double> phase_sum{0.0, 0.0};

    for (std::size_t i = 1; i < x.size(); ++i) {
        phase_sum +=
            std::conj(std::complex<double>(x[i - 1])) *
            std::complex<double>(x[i]);
    }

    const double phase_step = std::arg(phase_sum);

    return phase_step * sample_rate_hz / (2.0 * kPi);
}

} // namespace

int main()
{
    // Float oscillator: sustained operation, phase continuity and amplitude stability.
    for (float shift : {780.0f, -780.0f, 657.0f, 0.0f}) {
        std::vector<std::complex<float>> signal(64000 * 10, {1.0f, 0.0f});
        hro::dsp::FloatNcoShifter fast(64000.0f);
        fast.setFrequencyShift(shift);
        for (std::size_t offset = 0; offset < signal.size(); offset += 137) {
            fast.process(signal.data() + offset, std::min<std::size_t>(137, signal.size() - offset));
        }
        assert(std::abs(estimateFrequency(signal, 64000.0) - shift) < 0.01);
        for (const auto sample : signal) assert(std::abs(std::abs(sample) - 1.0f) < 0.001f);
        fast.reset();
        std::complex<float> first{1.0f, 0.0f}; fast.process(&first, 1);
        assert(first == std::complex<float>(1.0f, 0.0f));
    }

    constexpr double sample_rate = 64000.0;
    constexpr double shift_hz = 780.0;
    constexpr std::size_t sample_count = 64000;

    //
    // Test 1:
    // DC input must become a +780 Hz complex tone.
    //
    std::vector<std::complex<float>> samples(
        sample_count,
        {1.0f, 0.0f});

    hro::dsp::NcoShifter nco(sample_rate);
    nco.setFrequencyShift(shift_hz);
    nco.process(samples.data(), samples.size());

    const double measured_hz =
        estimateFrequency(samples, sample_rate);

    std::cout << "Requested shift : "
              << shift_hz << " Hz\n";

    std::cout << "Measured shift  : "
              << measured_hz << " Hz\n";

    assert(std::abs(measured_hz - shift_hz) < 0.01);

    //
    // Test 2:
    // Processing in separate blocks must produce the same
    // continuous waveform as processing one whole block.
    //
    std::vector<std::complex<float>> whole(
        sample_count,
        {1.0f, 0.0f});

    std::vector<std::complex<float>> split(
        sample_count,
        {1.0f, 0.0f});

    hro::dsp::NcoShifter nco_whole(sample_rate);
    nco_whole.setFrequencyShift(shift_hz);
    nco_whole.process(whole.data(), whole.size());

    hro::dsp::NcoShifter nco_split(sample_rate);
    nco_split.setFrequencyShift(shift_hz);

    constexpr std::size_t first_block = 12345;

    nco_split.process(
        split.data(),
        first_block);

    nco_split.process(
        split.data() + first_block,
        split.size() - first_block);

    float max_difference = 0.0f;

    for (std::size_t i = 0; i < sample_count; ++i) {
        const float difference =
            std::abs(whole[i] - split[i]);

        if (difference > max_difference) {
            max_difference = difference;
        }
    }

    std::cout << "Block max diff  : "
              << max_difference << "\n";

    assert(max_difference < 1.0e-6f);

    //
    // Test 3:
    // reset() must restore the initial phase.
    //
    nco.reset();

    std::vector<std::complex<float>> reset_samples(
        1024,
        {1.0f, 0.0f});

    nco.process(
        reset_samples.data(),
        reset_samples.size());

    assert(std::abs(reset_samples[0].real() - 1.0f) < 1.0e-6f);
    assert(std::abs(reset_samples[0].imag()) < 1.0e-6f);

    std::cout << "NCO shifter test passed.\n";

    return 0;
}
