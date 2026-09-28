#include "hro_config.h"

#include <iostream>

int main()
{
    HroConfig config;

    const bool loaded = config.load("/etc/hro/config.ini");

    std::cout << "Load result: "
          << (loaded ? "OK" : "FAILED - using defaults")
          << '\n';

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

    std::cout << "Screenshot Prefix: "
              << config.screenshot_prefix << '\n';

    std::cout << "\n--- Save test ---\n";

    const std::string test_file = "/tmp/hro_config_test.ini";

    if (config.save(test_file))
        std::cout << "Save result: OK\n";
    else
        std::cout << "Save result: FAILED\n";

    std::cout << "\n--- Invalid save test ---\n";

    config.frequency_hz = 0;  // intentionally invalid

    if (config.save(test_file))
        std::cout << "Invalid save result: UNEXPECTED SUCCESS\n";
    else
        std::cout << "Invalid save result: REJECTED\n";

    return 0;
}
