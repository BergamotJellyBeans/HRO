#include "dsp/fs4_rotator.h"
#include "dsp/decimator_15.h"
#include "dsp/nco_shifter.h"
#include "dsp/resampler_16_125.h"

#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

double estimateFrequency(const std::vector<std::complex<float>>& x,
                         double sample_rate_hz,
                         std::size_t skip)
{
    std::complex<double> phase_sum{0.0, 0.0};

    for (std::size_t i = skip + 1; i < x.size(); ++i) {
        phase_sum +=
            std::conj(std::complex<double>(x[i - 1])) *
            std::complex<double>(x[i]);
    }

    return std::arg(phase_sum) *
           sample_rate_hz / (2.0 * kPi);
}

} // namespace

int main()
{
    constexpr double input_rate = 960000.0;
    constexpr double input_frequency = -240000.0;
    constexpr double nco_shift = 780.0;
    constexpr double output_rate = 8192.0;

    //
    // Generate one second of synthetic IQ.
    // -240 kHz represents the wanted RF signal when
    // the RTL-SDR LO is tuned +Fs/4 above it.
    //
    std::vector<std::complex<float>> input(960000);

    for (std::size_t n = 0; n < input.size(); ++n) {
        const double phase =
            2.0 * kPi * input_frequency *
            static_cast<double>(n) / input_rate;

        input[n] = {
            static_cast<float>(std::cos(phase)),
            static_cast<float>(std::sin(phase))
        };
    }

    //
    // 1. Fs/4 frequency translation.
    //
    hro::dsp::Fs4Rotator fs4;
    fs4.process(input.data(), input.size());

    //
    // 2. 960 kS/s -> 64 kS/s.
    //
    hro::dsp::Decimator15 decimator;

    std::vector<std::complex<float>> decimated;
    decimated.reserve(64000);

    decimator.process(
        input.data(),
        input.size(),
        decimated);

    std::cout << "Decimator output : "
              << decimated.size()
              << " samples\n";

    assert(decimated.size() == 64000);

    //
    // 3. Move wanted signal from DC to the user-selected
    //    FFT center frequency.
    //
    hro::dsp::NcoShifter nco(64000.0);
    nco.setFrequencyShift(nco_shift);
    nco.process(decimated.data(), decimated.size());

    //
    // 4. 64 kS/s -> 8192 S/s.
    //
    hro::dsp::Resampler16_125 resampler;

    std::vector<std::complex<float>> output;
    output.reserve(8192);

    resampler.process(
        decimated.data(),
        decimated.size(),
        output);

    std::cout << "Resampler output : "
              << output.size()
              << " samples\n";

    assert(output.size() == 8192);

    //
    // Skip the beginning because FIR filters contain
    // startup transient samples.
    //
    constexpr std::size_t skip = 256;

    const double measured_frequency =
        estimateFrequency(output, output_rate, skip);

    std::cout << "Expected frequency: "
              << nco_shift << " Hz\n";

    std::cout << "Measured frequency: "
              << measured_frequency << " Hz\n";

    assert(std::abs(measured_frequency - nco_shift) < 0.1);

    std::cout << "DSP chain test passed.\n";

    return 0;
}
