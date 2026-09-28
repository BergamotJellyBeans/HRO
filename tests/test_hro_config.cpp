#include "hro_config.h"

#include <iostream>

int main()
{
    HroConfig config;

    if (!config.load("/etc/hro/config.ini"))
    {
        std::cerr << "Failed to load config.ini\n";
        return 1;
    }

    std::cout << "Observer: " << config.observer << '\n';
    std::cout << "Location: " << config.location << '\n';
    std::cout << "Latitude: " << config.latitude << '\n';
    std::cout << "Longitude: " << config.longitude << '\n';

    std::cout << "Receiver: " << config.receiver << '\n';
    std::cout << "Frequency: " << config.frequency_hz << " Hz\n";
    std::cout << "FFT Center: " << config.fft_center_hz << " Hz\n";
    std::cout << "FFT Range: +/-" << config.fft_range_hz << " Hz\n";
    std::cout << "Level Peak Range: +/-"
              << config.level_peak_range_hz << " Hz\n";
    std::cout << "Antenna: " << config.antenna << '\n';

    std::cout << "Audio Volume: " << config.volume << '\n';
    std::cout << "Audio Mute: " << config.mute << '\n';

    std::cout << "Screenshot Prefix: "
              << config.screenshot_prefix << '\n';

    return 0;
}
