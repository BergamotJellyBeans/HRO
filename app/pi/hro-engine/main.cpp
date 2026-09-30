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

#include <ctime>
#include <iomanip>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <csignal>

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

namespace
{
    volatile std::sig_atomic_t g_stopRequested = 0;

    void signalHandler(int)
    {
        g_stopRequested = 1;
    }
}

int main()
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    constexpr uint32_t SAMPLE_RATE = 960000;
    constexpr std::size_t BUFFER_SIZE = 262144;

    constexpr uint16_t HRO_LIVE_UDP_PORT  = 50000;
    constexpr uint16_t HRO_PNG_UDP_PORT   = 50001;
    constexpr uint16_t HRO_AUDIO_UDP_PORT = 50002;

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

    sockaddr_in pngAddress{};
    pngAddress.sin_family = AF_INET;
    pngAddress.sin_port = htons(HRO_PNG_UDP_PORT);

    if (::inet_pton(
        AF_INET,
        "127.0.0.1",
        &pngAddress.sin_addr) != 1)
    {
        std::cerr << "ERROR: Invalid PNG destination address\n";
        ::close(liveSocket);
        return 1;
    }

    sockaddr_in audioAddress{};
    audioAddress.sin_family = AF_INET;
    audioAddress.sin_port = htons(HRO_AUDIO_UDP_PORT);

    if (::inet_pton(
        AF_INET,
        "127.0.0.1",
        &audioAddress.sin_addr) != 1)
    {
        std::cerr << "ERROR: Invalid AUDIO destination address\n";
        ::close(liveSocket);
        return 1;
    }

    std::cout
        << "LIVE UDP destination: 127.0.0.1:"
        << HRO_LIVE_UDP_PORT << '\n';

    std::cout
        << "PNG UDP destination: 127.0.0.1:"
        << HRO_PNG_UDP_PORT << '\n';

    std::cout
        << "AUDIO UDP destination: 127.0.0.1:"
        << HRO_AUDIO_UDP_PORT << '\n';

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

    constexpr uint32_t RTL_SAMPLE_RATE = 960000;
    constexpr uint32_t FS4_HZ = RTL_SAMPLE_RATE / 4U;

    const uint32_t target_frequency_hz =
        static_cast<uint32_t>(config.frequency_hz);

    const uint32_t lo_frequency_hz =
        target_frequency_hz + FS4_HZ;

    if (!sdr.setCenterFrequency(lo_frequency_hz))
    {
        std::cerr << "ERROR: Failed to set center frequency\n";
        return 1;
    }

    std::cout
        << "Target RF: " << target_frequency_hz << " Hz\n"
        << "RTL-SDR LO: " << lo_frequency_hz << " Hz\n"
        << "Fs/4 shift: +" << FS4_HZ << " Hz\n";

    if (!sdr.setTunerGain(402))
    {
        std::cerr << "ERROR: Failed to set RTL-SDR tuner gain\n";
        return 1;
    }

    std::cout << "RTL-SDR tuner gain: 40.2 dB (manual)\n";

    if (!sdr.resetBuffer())
    {
        std::cerr << "ERROR: Failed to reset RTL-SDR buffer\n";
        return 1;
    }

    std::vector<uint8_t> rawBuffer(BUFFER_SIZE);

    std::vector<std::complex<float>> iqBuffer;
    iqBuffer.reserve(BUFFER_SIZE / 2);

    // RTL-SDR acquisition -> DSP queue
    std::deque<std::vector<uint8_t>> rawQueue;
    std::mutex rawQueueMutex;
    std::condition_variable rawQueueCv;
    std::atomic<bool> acquisitionRunning{true};

    constexpr std::size_t MAX_RAW_QUEUE = 4;

    hro::dsp::Fs4Rotator fs4Rotator;
    hro::dsp::Decimator15 decimator;
    std::vector<std::complex<float>> decimatedBuffer;

    hro::dsp::NcoShifter ncoShifter(64000.0);

    ncoShifter.setFrequencyShift(
        static_cast<double>(config.fft_center_hz)
    );

    hro::dsp::Resampler16_125 resampler;
    std::vector<std::complex<float>> resampledBuffer;
    std::vector<std::complex<float>> fftInputBuffer;
    fftInputBuffer.reserve(8192);

    constexpr std::size_t AUDIO_PACKET_SAMPLES = 256;
    std::array<float, AUDIO_PACKET_SAMPLES> audioBuffer{};
    std::size_t audioBufferCount = 0;

    std::uint64_t totalBytes = 0;
    std::uint64_t totalDecimatedSamples = 0;
    std::uint64_t totalResampledSamples = 0;

    std::cout << "RTL-SDR acquisition started.\n";
    const auto acquisitionStart =
        std::chrono::steady_clock::now();

    std::atomic<uint64_t> rawQueueOverflowCount{0};

    std::thread acquisitionThread([&]()
    {
        std::vector<uint8_t> acquisitionBuffer(BUFFER_SIZE);

        while (acquisitionRunning)
        {
            int bytesRead = 0;

            if (!sdr.read(acquisitionBuffer, bytesRead))
            {
                std::cerr << "ERROR: RTL-SDR read failed\n";
                acquisitionRunning = false;
                rawQueueCv.notify_all();
                break;
            }

            if (bytesRead <= 0)
                continue;

            std::vector<uint8_t> block(
                acquisitionBuffer.begin(),
                acquisitionBuffer.begin() + bytesRead
            );

            {
                std::lock_guard<std::mutex> lock(rawQueueMutex);

                // Observation must stay real-time.
                // Never allow an unlimited backlog.
                if (rawQueue.size() >= MAX_RAW_QUEUE)
                {
                    rawQueue.pop_front();
                    ++rawQueueOverflowCount;

                    std::cerr
                        << "WARNING: Raw queue overflow - oldest block dropped"
                        << "  count=" << rawQueueOverflowCount.load()
                        << '\n';
                }

                rawQueue.push_back(std::move(block));
            }

            rawQueueCv.notify_one();
        }
    });

    while (!g_stopRequested)
    {
        int bytesRead = 0;

        const auto readStart =
            std::chrono::steady_clock::now();

        {
            std::unique_lock<std::mutex> lock(rawQueueMutex);

            rawQueueCv.wait(lock, [&]()
            {
                return !rawQueue.empty() || !acquisitionRunning;
            });

            if (rawQueue.empty() && !acquisitionRunning)
            {
                std::cerr << "ERROR: RTL-SDR acquisition stopped\n";
                break;
            }

            rawBuffer = std::move(rawQueue.front());
            rawQueue.pop_front();
        }

        bytesRead = static_cast<int>(rawBuffer.size());

        const auto readEnd =
            std::chrono::steady_clock::now();

        iqBuffer.clear();

        const std::size_t sampleCount =
            static_cast<std::size_t>(bytesRead) / 2;

        iqBuffer.resize(sampleCount);
        const auto t0 = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            const float I =
                (static_cast<float>(rawBuffer[i * 2]) - 127.5f) / 127.5f;

            const float Q =
                (static_cast<float>(rawBuffer[i * 2 + 1]) - 127.5f) / 127.5f;

            iqBuffer[i] = std::complex<float>(I, Q);
        }
        const auto t1 = std::chrono::steady_clock::now();

        fs4Rotator.process(
            iqBuffer.data(),
            iqBuffer.size()
        );
        const auto t2 = std::chrono::steady_clock::now();

        decimatedBuffer.clear();

        decimator.process(
            iqBuffer.data(),
            iqBuffer.size(),
            decimatedBuffer
        );
        const auto t3 = std::chrono::steady_clock::now();

        ncoShifter.process(
            decimatedBuffer.data(),
            decimatedBuffer.size()
        );
        const auto t4 = std::chrono::steady_clock::now();

        resampledBuffer.clear();

        resampler.process(
            decimatedBuffer.data(),
            decimatedBuffer.size(),
            resampledBuffer
        );
        const auto t5 = std::chrono::steady_clock::now();

        auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
#if 0
        std::cout
            << "\nIQ="        << ms(t0, t1) << " ms"
            << "  Fs4="      << ms(t1, t2) << " ms"
            << "  Decimator=" << ms(t2, t3) << " ms"
            << "  NCO="      << ms(t3, t4) << " ms"
            << "  Resampler=" << ms(t4, t5) << " ms"
            << '\n';
#endif
        const auto dspEnd =
            std::chrono::steady_clock::now();

        const double readMs =
            std::chrono::duration<double, std::milli>(
                readEnd - readStart).count();

        const double dspMs =
            std::chrono::duration<double, std::milli>(
                dspEnd - readEnd).count();
#if 0
        std::cout
            << "\nREAD=" << readMs << " ms"
            << "  DSP=" << dspMs << " ms"
            << '\n';
#endif
        for (const auto& sample : resampledBuffer)
        {
            // --------------------------------------------------------
            // Web Audio
            // --------------------------------------------------------

            audioBuffer[audioBufferCount++] =
                sample.real();

            if (audioBufferCount == AUDIO_PACKET_SAMPLES)
            {
                const ssize_t audioSent =
                    ::sendto(
                        liveSocket,
                        audioBuffer.data(),
                        audioBuffer.size() * sizeof(float),
                        0,
                        reinterpret_cast<const sockaddr*>(&audioAddress),
                        sizeof(audioAddress));

                if (audioSent !=
                    static_cast<ssize_t>(
                        audioBuffer.size() * sizeof(float)))
                {
                    std::cerr
                        << "WARNING: AUDIO UDP send failed\n";
                }

                audioBufferCount = 0;
            }

            // --------------------------------------------------------
            // Existing FFT
            // --------------------------------------------------------

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

                const int displayStartBin =
                    config.fft_center_hz - DISPLAY_RANGE_HZ;

                const int displayEndBin =
                    config.fft_center_hz + DISPLAY_RANGE_HZ;

                for (int bin = displayStartBin;
                    bin <= displayEndBin;
                    ++bin)
                {
                    displaySpectrum.push_back(
                        fftBuffer[static_cast<std::size_t>(bin)]
                    );
                }
#if 0
                std::cout
                    << "\nFFT ready: "
                    << fftBuffer.size()
                    << " bins"
                    << "  Display: "
                    << displaySpectrum.size()
                    << " bins\n";
#endif
                fftInputBuffer.clear();

                std::vector<float> displayDb;
                displayDb.reserve(DISPLAY_BINS);

                for (const auto& value : displaySpectrum)
                {
                    const float power =
                        std::norm(value);

                    const float db =
                        10.0f * std::log10(
                            power + 1.0e-20f
                        );

                    displayDb.push_back(db);
                }

                const auto [minIt, maxIt] =
                    std::minmax_element(
                        displayDb.begin(),
                        displayDb.end()
                    );
#if 0
                std::cout
                    << "\nDisplay dB: min="
                    << *minIt
                    << "  max="
                    << *maxIt
                    << '\n';
#endif
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
#if 0
                std::cout
                    << "Peak dB: "
                    << peakDb
                    << '\n';
#endif
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

                    const auto debugNow = std::chrono::system_clock::now();
                    const std::time_t debugTime =
                        std::chrono::system_clock::to_time_t(debugNow);

                    std::tm debugTm{};
                    localtime_r(&debugTime, &debugTm);
#if 0
                    std::cout
                        << std::put_time(&debugTm, "%H:%M:%S")
                        << "  seq=" << liveSequence
                        << '\n';
#endif
                    const double elapsedSec =
                        std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - acquisitionStart
                        ).count();

                    const double rtlSampleRate =
                        (static_cast<double>(totalBytes) / 2.0) / elapsedSec;

                    const double resampledRate =
                        static_cast<double>(totalResampledSamples) / elapsedSec;
#if 0
                    std::cout
                        << "  elapsed=" << elapsedSec
                        << " s"
                        << "  RTL=" << rtlSampleRate
                        << " sample/s"
                        << "  Resampled=" << resampledRate
                        << " sample/s"
                        << "  QueueOverflow=" << rawQueueOverflowCount.load()
                        << '\n';
#endif
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

                const ssize_t liveSent =
                    ::sendto(
                        liveSocket,
                        livePacket.data(),
                        livePacket.size(),
                        0,
                        reinterpret_cast<const sockaddr*>(&liveAddress),
                        sizeof(liveAddress));

                if (liveSent != static_cast<ssize_t>(livePacket.size()))
                {
                    std::cerr
                        << "WARNING: LIVE UDP send failed\n";
                }

                const ssize_t pngSent =
                    ::sendto(
                        liveSocket,
                        livePacket.data(),
                        livePacket.size(),
                        0,
                        reinterpret_cast<const sockaddr*>(&pngAddress),
                        sizeof(pngAddress));

                if (pngSent != static_cast<ssize_t>(livePacket.size()))
                {
                    std::cerr
                        << "WARNING: PNG UDP send failed\n";
                }
            }           
        }        

        totalResampledSamples += resampledBuffer.size();
        totalDecimatedSamples += decimatedBuffer.size();
        totalBytes += static_cast<std::uint64_t>(bytesRead);
#if 0
        std::cout
            << "\rReceived: "
            << totalBytes
            << " bytes"
            << "  Decimated: "
            << totalDecimatedSamples
            << "  Resampled: "
            << totalResampledSamples
            << std::flush;
#endif
    }
    std::cout << "\nStopping HRO engine...\n";

    acquisitionRunning = false;
    rawQueueCv.notify_all();

    if (acquisitionThread.joinable())
    {
        acquisitionThread.join();
    }

    ::close(liveSocket);

    std::cout << "HRO engine stopped.\n";

    return 0;
}
