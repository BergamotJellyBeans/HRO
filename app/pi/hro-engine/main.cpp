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
#include <cmath>
#include <algorithm>
#include <cstring>

#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace
{

constexpr uint32_t HRO_LIVE_MAGIC = 0x48524F31;  // "HRO1"
constexpr uint16_t HRO_LIVE_VERSION = 1;
constexpr std::size_t HRO_LIVE_FFT_BINS = 501;

constexpr std::size_t HRO_LIVE_HEADER_SIZE = 28;

constexpr std::size_t HRO_LIVE_PACKET_SIZE =
    HRO_LIVE_HEADER_SIZE +
    HRO_LIVE_FFT_BINS * sizeof(float);

static_assert(
    HRO_LIVE_PACKET_SIZE == 2032,
    "Unexpected HRO Live UDP packet size"
);

void writeUint16BE(uint8_t* p, uint16_t value)
{
    p[0] = static_cast<uint8_t>(value >> 8);
    p[1] = static_cast<uint8_t>(value);
}

void writeUint32BE(uint8_t* p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

void writeUint64BE(uint8_t* p, uint64_t value)
{
    for (int i = 7; i >= 0; --i)
    {
        p[i] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

void writeFloat32BE(uint8_t* p, float value)
{
    static_assert(sizeof(float) == sizeof(uint32_t));

    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));

    writeUint32BE(p, bits);
}

} // namespace

int main()
{
    constexpr uint32_t SAMPLE_RATE = 960000;
    constexpr std::size_t BUFFER_SIZE = 262144;
    constexpr uint16_t HRO_LIVE_UDP_PORT = 50000;

    const int liveSocket =
        ::socket(AF_INET, SOCK_DGRAM, 0);

    if (liveSocket < 0)
    {
        std::cerr << "ERROR: Failed to create LIVE UDP socket\n";
        return 1;
    }

    sockaddr_in liveAddress{};
    liveAddress.sin_family = AF_INET;
    liveAddress.sin_port = htons(HRO_LIVE_UDP_PORT);

    if (::inet_pton(
        AF_INET,
        "127.0.0.1",
        &liveAddress.sin_addr) != 1)
    {
        std::cerr << "ERROR: Invalid LIVE destination address\n";
        ::close(liveSocket);
        return 1;
    }

    std::cout
        << "LIVE UDP destination: 127.0.0.1:"
        << HRO_LIVE_UDP_PORT
        << '\n';

    uint64_t liveSequence = 0;

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

    ncoShifter.setFrequencyShift(
        -static_cast<double>(config.fft_center_hz)
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

                constexpr int DISPLAY_RANGE_HZ = 250;
                constexpr int FFT_SIZE = 8192;
                constexpr int DISPLAY_BINS = DISPLAY_RANGE_HZ * 2 + 1;

                std::vector<std::complex<float>> displaySpectrum;
                displaySpectrum.reserve(DISPLAY_BINS);

                for (int frequency = -DISPLAY_RANGE_HZ;
                    frequency <= DISPLAY_RANGE_HZ;
                    ++frequency)
                {
                    const int bin =
                        (frequency >= 0)
                            ? frequency
                            : FFT_SIZE + frequency;

                    displaySpectrum.push_back(
                        fftBuffer[static_cast<std::size_t>(bin)]
                    );
                }

                std::cout
                    << "\nFFT ready: "
                    << fftBuffer.size()
                    << " bins"
                    << "  Display: "
                    << displaySpectrum.size()
                    << " bins\n";

                fftInputBuffer.clear();

                constexpr float HANN_COHERENT_GAIN = 0.5f;
                constexpr float FFT_NORMALIZATION =
                    static_cast<float>(FFT_SIZE) * HANN_COHERENT_GAIN;

                std::vector<float> displayDb;
                displayDb.reserve(DISPLAY_BINS);

                for (const auto& value : displaySpectrum)
                {
                    const float magnitude =
                        std::abs(value) / FFT_NORMALIZATION;

                    const float db =
                        20.0f * std::log10(
                            std::max(magnitude, 1.0e-12f)
                        );

                    displayDb.push_back(db);
                }

                const auto [minIt, maxIt] =
                    std::minmax_element(
                        displayDb.begin(),
                        displayDb.end()
                    );

                std::cout
                    << "\nDisplay dB: min="
                    << *minIt
                    << "  max="
                    << *maxIt
                    << '\n';

                const int peakRange =
                    config.level_peak_range_hz;

                const int centerIndex =
                    DISPLAY_RANGE_HZ;

                const int peakStart =
                    centerIndex - peakRange;

                const int peakEnd =
                    centerIndex + peakRange;

                float peakDb =
                    displayDb[static_cast<std::size_t>(peakStart)];

                for (int i = peakStart + 1; i <= peakEnd; ++i)
                {
                    peakDb = std::max(
                        peakDb,
                        displayDb[static_cast<std::size_t>(i)]
                    );
                }

                std::cout
                    << "Peak dB: "
                    << peakDb
                    << '\n';

                std::vector<uint8_t> livePacket(
                    HRO_LIVE_PACKET_SIZE);

                writeUint32BE(
                    livePacket.data() + 0,
                    HRO_LIVE_MAGIC);

                writeUint16BE(
                    livePacket.data() + 4,
                    HRO_LIVE_VERSION);

                writeUint16BE(
                    livePacket.data() + 6,
                    static_cast<uint16_t>(displayDb.size()));

                writeUint64BE(
                    livePacket.data() + 8,
                    liveSequence++);

                const auto now =
                    std::chrono::system_clock::now();

                const int64_t timestampMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()
                    ).count();

                writeUint64BE(
                    livePacket.data() + 16,
                    static_cast<uint64_t>(timestampMs));

                writeFloat32BE(
                    livePacket.data() + 24,
                    peakDb);

                for (std::size_t i = 0;
                     i < displayDb.size();
                     ++i)
                {
                    writeFloat32BE(
                        livePacket.data() +
                            HRO_LIVE_HEADER_SIZE +
                            i * sizeof(float),
                        displayDb[i]);
                }

                const ssize_t sent =
                    ::sendto(
                        liveSocket,
                        livePacket.data(),
                        livePacket.size(),
                        0,
                        reinterpret_cast<const sockaddr*>(&liveAddress),
                        sizeof(liveAddress));

                if (sent != static_cast<ssize_t>(livePacket.size()))
                {
                    std::cerr
                        << "WARNING: LIVE UDP send failed\n";
                }
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
