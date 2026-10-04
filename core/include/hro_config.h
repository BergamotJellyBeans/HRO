#pragma once

#include <cstdint>
#include <string>

struct HroConfig
{
    // Station
    std::string observer;
    std::string location;
    double latitude = 0.0;
    double longitude = 0.0;

    // Receiver
    std::string receiver;
    uint32_t frequency_hz = 0;
    int sdr_gain = 0;
    int fft_center_hz = 0;
    std::string antenna;
    int level_peak_range_hz = 0;

    // Local Pi5 waterfall appearance; never applied to transmitted FFT data.
    int display_level_db = 0;

    // Screenshot
    std::string screenshot_prefix;

    bool load(const std::string& filename);
    bool save(const std::string& filename) const;
    bool validate(std::string& error_message) const;
    void setDefaults();
};
