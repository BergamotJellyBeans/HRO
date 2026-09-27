#include "dsp/resampler_16_125.h"

#include <cassert>
#include <complex>
#include <iostream>
#include <vector>
#include <cmath>

using hro::dsp::Resampler16_125;

int main()
{
    using Complex = std::complex<float>;

    // Exactly one second at 64 kS/s.
    constexpr std::size_t inputCount = 64000;

    std::vector<Complex> input(
        inputCount,
        Complex{1.0f, 0.0f}
    );

    Resampler16_125 resampler;

    std::vector<Complex> output;

    resampler.process(
        input.data(),
        input.size(),
        output
    );

    std::cout
        << "Input samples  : "
        << input.size()
        << "\n";

    std::cout
        << "Output samples : "
        << output.size()
        << "\n";

    // 64000 * 16 / 125 = 8192
    assert(output.size() == 8192);

    std::cout
        << "Resampler16_125 sample-count test passed\n";

    // --------------------------------------------------------
    // 1 kHz complex-tone test
    // --------------------------------------------------------

    constexpr double pi = 3.14159265358979323846;
    constexpr double inputFs = 64000.0;
    constexpr double outputFs = 8192.0;
    constexpr double toneHz = 1000.0;

    std::vector<Complex> tone(inputCount);

    for (std::size_t n = 0; n < inputCount; ++n) {
        const double phase =
            2.0 * pi * toneHz *
            static_cast<double>(n) / inputFs;

        tone[n] = Complex{
            static_cast<float>(std::cos(phase)),
            static_cast<float>(std::sin(phase))
        };
    }

    Resampler16_125 toneResampler;
    std::vector<Complex> toneOutput;

    toneResampler.process(
        tone.data(),
        tone.size(),
        toneOutput
    );

    assert(toneOutput.size() == 8192);

    // Ignore the FIR startup transient.
    constexpr std::size_t skip = 256;

    // Measure RMS magnitude.
    double power = 0.0;
    std::size_t samples = 0;

    for (std::size_t i = skip; i < toneOutput.size(); ++i) {
        power += static_cast<double>(std::norm(toneOutput[i]));
        ++samples;
    }

    const double amplitude =
        std::sqrt(power / static_cast<double>(samples));

    const double amplitudeDb =
        20.0 * std::log10(amplitude);

    // Measure frequency from average phase advance.
    Complex phaseSum{0.0f, 0.0f};

    for (std::size_t i = skip + 1; i < toneOutput.size(); ++i) {
        phaseSum +=
            toneOutput[i] *
            std::conj(toneOutput[i - 1]);
    }

    const double phaseAdvance =
        std::atan2(
            static_cast<double>(phaseSum.imag()),
            static_cast<double>(phaseSum.real())
        );

    const double measuredFrequency =
        phaseAdvance * outputFs /
        (2.0 * pi);

    std::cout
        << "1 kHz amplitude : "
        << amplitude
        << " ("
        << amplitudeDb
        << " dB)\n";

    std::cout
        << "1 kHz measured frequency : "
        << measuredFrequency
        << " Hz\n";

    // --------------------------------------------------------
    // Block-boundary continuity test
    // --------------------------------------------------------

    Resampler16_125 splitResampler;
    std::vector<Complex> splitOutput;

    const std::size_t blockSizes[] = {
        137,
        311,
        1024,
        73,
        2000,
        509
    };

    std::size_t inputPos = 0;
    std::size_t blockIndex = 0;

    while (inputPos < tone.size()) {

        std::size_t blockSize =
            blockSizes[
                blockIndex %
                (sizeof(blockSizes) / sizeof(blockSizes[0]))
            ];

        if (inputPos + blockSize > tone.size()) {
            blockSize = tone.size() - inputPos;
        }

        splitResampler.process(
            tone.data() + inputPos,
            blockSize,
            splitOutput
        );

        inputPos += blockSize;
        ++blockIndex;
    }

    std::cout
        << "Split-block output samples : "
        << splitOutput.size()
        << "\n";

    assert(splitOutput.size() == toneOutput.size());

    double maxDifference = 0.0;

    for (std::size_t i = 0; i < toneOutput.size(); ++i) {

        const double difference =
            static_cast<double>(
                std::abs(
                    toneOutput[i] -
                    splitOutput[i]
                )
            );

        if (difference > maxDifference) {
            maxDifference = difference;
        }
    }

    std::cout
        << "Split-block max difference : "
        << maxDifference
        << "\n";

    assert(maxDifference < 1e-6);

    std::cout
        << "Block-boundary continuity test passed\n";        

    // --------------------------------------------------------
    // Frequency-response test
    // --------------------------------------------------------

    auto measureResamplerTone = [&](double frequency) {

        std::vector<Complex> testTone(inputCount);

        for (std::size_t n = 0; n < inputCount; ++n) {
            const double phase =
                2.0 * pi * frequency *
                static_cast<double>(n) / inputFs;

            testTone[n] = Complex{
                static_cast<float>(std::cos(phase)),
                static_cast<float>(std::sin(phase))
            };
        }

        Resampler16_125 r;
        std::vector<Complex> out;

        r.process(
            testTone.data(),
            testTone.size(),
            out
        );

        // Ignore FIR startup transient.
        constexpr std::size_t responseSkip = 256;

        double responsePower = 0.0;
        std::size_t responseSamples = 0;

        for (std::size_t i = responseSkip; i < out.size(); ++i) {
            responsePower +=
                static_cast<double>(std::norm(out[i]));
            ++responseSamples;
        }

        const double amplitude =
            std::sqrt(
                responsePower /
                static_cast<double>(responseSamples)
            );

        return 20.0 * std::log10(amplitude);
    };

    const double responseFrequencies[] = {
        0.0,
        500.0,
        1000.0,
        2000.0,
        2500.0,
        3000.0,
        3500.0,
        4000.0,
        4096.0,
        4500.0,
        5000.0,
        6000.0,
        8000.0,
        10000.0
    };

    std::cout << "\nResampler frequency response:\n";

    for (double frequency : responseFrequencies) {

        const double db =
            measureResamplerTone(frequency);

        std::cout
            << frequency
            << " Hz : "
            << db
            << " dB\n";
    }

assert(std::abs(measureResamplerTone(1000.0)) < 0.01);
assert(std::abs(measureResamplerTone(2000.0)) < 0.01);
assert(measureResamplerTone(4096.0) < -80.0);
assert(measureResamplerTone(5000.0) < -80.0);    

    return 0;
}

