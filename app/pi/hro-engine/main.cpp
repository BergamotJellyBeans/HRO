#include "rtl_sdr_source.h"
#include "hro_config.h"
#include "dsp/fs4_rotator.h"
#include "dsp/decimator_15.h"
#include "dsp/nco_shifter.h"
#include "dsp/resampler_16_125.h"
#include "dsp/hann_window.h"
#include "dsp/complex_fft.h"

#include <cstdint>
#include <iostream>
#include <vector>
#include <complex>

int main()
{
    constexpr uint32_t SAMPLE_RATE = 960000;
    constexpr std::size_t BUFFER_SIZE = 262144;

    HroConfig config;

    if (!config.load("/etc/hro/config.ini"))
    {
        std::cerr
            << "WARNING: Failed to load /etc/hro/config.ini\n"
            << "Using default configuration.\n";
    }

    std::cout
        << "Pi5-HRO Engine\n"
        << "Sample rate : " << SAMPLE_RATE << " Hz\n"
        << "Frequency   : " << config.frequency_hz << " Hz\n";

    RtlSdrSource sdr;

    if (!sdr.open())
    {
        std::cerr << "ERROR: Failed to open RTL-SDR\n";
        return 1;
    }

    if (!sdr.setSampleRate(SAMPLE_RATE))
    {
        std::cerr << "ERROR: Failed to set sample rate\n";
        return 1;
    }

    if (!sdr.setCenterFrequency(
            static_cast<uint32_t>(config.frequency_hz)))
    {
        std::cerr << "ERROR: Failed to set center frequency\n";
        return 1;
    }

    if (!sdr.resetBuffer())
    {
        std::cerr << "ERROR: Failed to reset RTL-SDR buffer\n";
        return 1;
    }

    std::vector<uint8_t> rawBuffer(BUFFER_SIZE);
    std::vector<std::complex<float>> iqBuffer;
    iqBuffer.reserve(BUFFER_SIZE / 2);

    hro::dsp::Fs4Rotator fs4Rotator;
    hro::dsp::Decimator15 decimator;
    std::vector<std::complex<float>> decimatedBuffer;

    hro::dsp::NcoShifter ncoShifter(64000.0);
    ncoShifter.process(
        decimatedBuffer.data(),
        decimatedBuffer.size()
    );

    hro::dsp::Resampler16_125 resampler;
    std::vector<std::complex<float>> resampledBuffer;
    std::vector<std::complex<float>> fftInputBuffer;
    fftInputBuffer.reserve(8192);

    std::uint64_t totalBytes = 0;
    std::uint64_t totalDecimatedSamples = 0;
    std::uint64_t totalResampledSamples = 0;

    std::cout << "RTL-SDR acquisition started.\n";

    while (true)
    {
        int bytesRead = 0;

        if (!sdr.read(rawBuffer, bytesRead))
        {
            std::cerr << "ERROR: RTL-SDR read failed\n";
            return 1;
        }

        iqBuffer.clear();

        const std::size_t sampleCount =
            static_cast<std::size_t>(bytesRead) / 2;

        iqBuffer.resize(sampleCount);

        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            const float I =
                (static_cast<float>(rawBuffer[i * 2]) - 127.5f) / 127.5f;

            const float Q =
                (static_cast<float>(rawBuffer[i * 2 + 1]) - 127.5f) / 127.5f;

            iqBuffer[i] = std::complex<float>(I, Q);
        }

        fs4Rotator.process(
            iqBuffer.data(),
            iqBuffer.size()
        );

        decimatedBuffer.clear();

        decimator.process(
            iqBuffer.data(),
            iqBuffer.size(),
            decimatedBuffer
        );

        ncoShifter.process(
            decimatedBuffer.data(),
            decimatedBuffer.size()
        );

        resampledBuffer.clear();

        resampler.process(
            decimatedBuffer.data(),
            decimatedBuffer.size(),
            resampledBuffer
        );

        for (const auto& sample : resampledBuffer)
        {
            fftInputBuffer.push_back(sample);

            if (fftInputBuffer.size() == 8192)
            {
                // FFT処理用に1秒分をコピー
                std::vector<std::complex<float>> fftBuffer = fftInputBuffer;

                // Periodic Hann window
                hro::dsp::HannWindow::apply(
                    fftBuffer.data(),
                    fftBuffer.size()
                );

                // 8192-point complex FFT
                if (!hro::dsp::ComplexFft::forward(
                        fftBuffer.data(),
                        fftBuffer.size()))
                {
                    std::cerr << "\nERROR: FFT failed\n";
                    return 1;
                }

                std::cout
                    << "\nFFT ready: "
                    << fftBuffer.size()
                    << " bins\n";

                fftInputBuffer.clear();
            }           
        }        

        totalResampledSamples += resampledBuffer.size();
        totalDecimatedSamples += decimatedBuffer.size();
        totalBytes += static_cast<std::uint64_t>(bytesRead);

        std::cout
            << "\rReceived: "
            << totalBytes
            << " bytes"
            << "  Decimated: "
            << totalDecimatedSamples
            << "  Resampled: "
            << totalResampledSamples
            << std::flush;
    }

    return 0;
}
