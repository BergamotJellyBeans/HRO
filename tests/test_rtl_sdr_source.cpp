#include "rtl_sdr_source.h"

#include <cstdint>
#include <iostream>
#include <vector>

int main()
{
    constexpr uint32_t sampleRate = 960000;
    constexpr uint32_t centerFrequency = 77400000;

    RtlSdrSource sdr;

    if (!sdr.open()) {
        std::cerr << "ERROR: Failed to open RTL-SDR\n";
        return 1;
    }

    if (!sdr.setSampleRate(sampleRate)) {
        std::cerr << "ERROR: Failed to set sample rate\n";
        return 1;
    }

    if (!sdr.setCenterFrequency(centerFrequency)) {
        std::cerr << "ERROR: Failed to set center frequency\n";
        return 1;
    }

    if (!sdr.resetBuffer()) {
        std::cerr << "ERROR: Failed to reset RTL-SDR buffer\n";
        return 1;
    }

    // 262144 bytes = 131072 complex IQ samples
    std::vector<uint8_t> buffer(262144);

    std::uint64_t totalBytes = 0;

    for (int i = 0; i < 20; ++i) {
        int bytesRead = 0;

        if (!sdr.read(buffer, bytesRead)) {
            std::cerr << "ERROR: RTL-SDR read failed\n";
            return 1;
        }

        totalBytes += static_cast<std::uint64_t>(bytesRead);

        std::cout
            << "Read " << bytesRead
            << " bytes, total " << totalBytes
            << '\n';
    }

    std::cout << "RTL-SDR read test completed successfully.\n";
    return 0;
}
