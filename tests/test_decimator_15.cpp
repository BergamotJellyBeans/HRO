#include "dsp/decimator_15.h"

#include <cassert>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

namespace {

bool nearlyEqual(
    const std::complex<float>& a,
    const std::complex<float>& b,
    float tolerance = 1.0e-6f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main()
{
    using hro::dsp::Decimator15;
    using Complex = std::complex<float>;

    // --------------------------------------------------------
    // Test 1:
    // 960 input samples must produce 64 output samples.
    // --------------------------------------------------------

    std::vector<Complex> input(960);

    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = {
            static_cast<float>(i) / 960.0f,
            static_cast<float>(i) / 1920.0f
        };
    }

    Decimator15 decimator1;

    std::vector<Complex> output1;

    decimator1.process(
        input.data(),
        input.size(),
        output1);

    assert(output1.size() == 64);

    // --------------------------------------------------------
    // Test 2:
    // Splitting the same input into arbitrary blocks must
    // produce exactly the same output sequence.
    // --------------------------------------------------------

    Decimator15 decimator2;

    std::vector<Complex> output2;

    const std::size_t block1 = 137;
    const std::size_t block2 = 311;
    const std::size_t block3 =
        input.size() - block1 - block2;

    decimator2.process(
        input.data(),
        block1,
        output2);

    decimator2.process(
        input.data() + block1,
        block2,
        output2);

    decimator2.process(
        input.data() + block1 + block2,
        block3,
        output2);

    assert(output2.size() == output1.size());

    for (std::size_t i = 0; i < output1.size(); ++i) {
        assert(nearlyEqual(output1[i], output2[i]));
    }

    // --------------------------------------------------------
    // Test 3:
    // reset() must restore the initial state.
    // --------------------------------------------------------

    decimator2.reset();

    std::vector<Complex> output3;

    decimator2.process(
        input.data(),
        input.size(),
        output3);

    assert(output3.size() == output1.size());

    for (std::size_t i = 0; i < output1.size(); ++i) {
        assert(nearlyEqual(output1[i], output3[i]));
    }

    // --------------------------------------------------------
    // Test 4:
    // Measure actual response using complex test tones.
    // --------------------------------------------------------

    constexpr double pi = 3.14159265358979323846;
    constexpr double inputFs = 960000.0;

    auto measureTone = [&](double frequency) {

        // Long enough to get past the FIR startup transient.
        constexpr std::size_t inputCount = 96000;

        std::vector<Complex> tone(inputCount);

        for (std::size_t n = 0; n < inputCount; ++n) {
            const double phase =
                2.0 * pi * frequency *
                static_cast<double>(n) / inputFs;

            tone[n] = Complex(
                static_cast<float>(std::cos(phase)),
                static_cast<float>(std::sin(phase)));
        }

        Decimator15 d;
        std::vector<Complex> out;

        d.process(tone.data(), tone.size(), out);

        // Ignore startup transient.
        constexpr std::size_t skip = 64;

        double power = 0.0;
        std::size_t samples = 0;

        for (std::size_t i = skip; i < out.size(); ++i) {
            power += static_cast<double>(std::norm(out[i]));
            ++samples;
        }

        return std::sqrt(power / static_cast<double>(samples));
    };

    const double amplitude5k  = measureTone(5000.0);
    const double amplitude10k = measureTone(10000.0);
    const double amplitude40k = measureTone(40000.0);

    const double db5k =
        20.0 * std::log10(amplitude5k);

    const double db10k =
        20.0 * std::log10(amplitude10k);

    const double db40k =
        20.0 * std::log10(amplitude40k);

    std::cout << "5 kHz response  : "
          << db5k << " dB\n";

    std::cout << "10 kHz response : "
          << db10k << " dB\n";

    std::cout << "40 kHz response : "
          << db40k << " dB\n";

// Passband must remain essentially flat.
assert(std::abs(db5k) < 0.01);
assert(std::abs(db10k) < 0.01);

// A representative stopband tone must be strongly rejected.
assert(db40k < -80.0);

    std::cout << "\nFrequency response:\n";

    const double testFrequencies[] = {
        0.0,
        5000.0,
        10000.0,
        15000.0,
        20000.0,
        25000.0,
        30000.0,
        32000.0,
        35000.0,
        40000.0,
        50000.0,
        64000.0,
        80000.0,
        100000.0
    };

    for (double frequency : testFrequencies) {
        const double amplitude = measureTone(frequency);

        const double db =
            20.0 * std::log10(amplitude);

        std::cout
            << frequency / 1000.0
            << " kHz : "
            << db
            << " dB\n";
    }

    std::cout << "Decimator15 test passed\n";

    return 0;
}
