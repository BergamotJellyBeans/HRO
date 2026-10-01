#include "hro_config.h"
#include "hro_fft_config.h"
#include "hro_sdr_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <cstdio>

namespace
{

std::string trim(const std::string& s)
{
    const auto first = std::find_if_not(
        s.begin(), s.end(),
        [](unsigned char c) { return std::isspace(c); });

    const auto last = std::find_if_not(
        s.rbegin(), s.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();

    if (first >= last)
        return {};

    return std::string(first, last);
}

} // namespace

bool HroConfig::load(const std::string& filename)
{
    HroConfig temp;
    temp.setDefaults();

    std::ifstream file(filename);

    if (!file)
    {
        setDefaults();
        return false;
    }

    std::string section;
    std::string line;

    struct
    {
        bool observer = false;
        bool location = false;
        bool latitude = false;
        bool longitude = false;

        bool receiver = false;
        bool frequency_hz = false;
        bool sdr_gain = false;
        bool fft_center_hz = false;
        bool antenna = false;
        bool level_peak_range_hz = false;

        bool screenshot_prefix = false;
    } seen;

    try
    {
        while (std::getline(file, line))
        {
            line = trim(line);

            if (line.empty())
                continue;

            if (line[0] == '#' || line[0] == ';')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto pos = line.find('=');

            if (pos == std::string::npos)
            {
                setDefaults();
                return false;
            }

            const std::string key =
                trim(line.substr(0, pos));

            const std::string value =
                trim(line.substr(pos + 1));

            if (section == "station")
            {
                if (key == "observer")
                {
                    temp.observer = value;
                    seen.observer = true;
                }
                else if (key == "location")
                {
                    temp.location = value;
                    seen.location = true;
                }
                else if (key == "latitude")
                {
                    temp.latitude = std::stod(value);
                    seen.latitude = true;
                }
                else if (key == "longitude")
                {
                    temp.longitude = std::stod(value);
                    seen.longitude = true;
                }
            }
            else if (section == "receiver")
            {
                if (key == "receiver")
                {
                    temp.receiver = value;
                    seen.receiver = true;
                }
                else if (key == "frequency_hz")
                {
                    temp.frequency_hz =
                        static_cast<uint32_t>(std::stoul(value));
                    seen.frequency_hz = true;
                }
                else if (key == "sdr_gain")
                {
                    temp.sdr_gain = std::stoi(value);
                    seen.sdr_gain = true;
                }
                else if (key == "fft_center_hz")
                {
                    temp.fft_center_hz = std::stoi(value);
                    seen.fft_center_hz = true;
                }
                else if (key == "antenna")
                {
                    temp.antenna = value;
                    seen.antenna = true;
                }
                else if (key == "level_peak_range_hz")
                {
                    temp.level_peak_range_hz = std::stoi(value);
                    seen.level_peak_range_hz = true;
                }
            }
            else if (section == "screenshot")
            {
                if (key == "prefix")
                {
                    temp.screenshot_prefix = value;
                    seen.screenshot_prefix = true;
                }
            }
        }
    }
    catch (...)
    {
        setDefaults();
        return false;
    }

    const bool all_required_keys_present =
        seen.observer &&
        seen.location &&
        seen.latitude &&
        seen.longitude &&
        seen.receiver &&
        seen.frequency_hz &&
        seen.sdr_gain &&
        seen.fft_center_hz &&
        seen.antenna &&
        seen.level_peak_range_hz &&
        seen.screenshot_prefix;

    if (!all_required_keys_present)
    {
        setDefaults();
        return false;
    }    

    std::string error_message;

    if (!temp.validate(error_message))
    {
        setDefaults();
        return false;
    }

    *this = temp;
    return true;
}

bool HroConfig::save(const std::string& filename) const
{
    std::string error_message;

    if (!validate(error_message))
        return false;

    const std::string temp_filename = filename + ".tmp";

    std::ofstream file(temp_filename);

    if (!file.is_open())
        return false;

    file << std::setprecision(10);

    file << "[station]\n";
    file << "observer=" << observer << "\n";
    file << "location=" << location << "\n";
    file << "latitude=" << latitude << "\n";
    file << "longitude=" << longitude << "\n";
    file << "\n";

    file << "[receiver]\n";
    file << "receiver=" << receiver << "\n";
    file << "frequency_hz=" << frequency_hz << "\n";
    file << "sdr_gain=" << sdr_gain << "\n";
    file << "fft_center_hz=" << fft_center_hz << "\n";
    file << "antenna=" << antenna << "\n";
    file << "level_peak_range_hz=" << level_peak_range_hz << "\n";
    file << "\n";

    file << "[screenshot]\n";
    file << "prefix=" << screenshot_prefix << "\n";

    file.flush();

    if (!file.good())
    {
        file.close();
        std::remove(temp_filename.c_str());
        return false;
    }

    file.close();

    if (std::rename(temp_filename.c_str(), filename.c_str()) != 0)
    {
        std::remove(temp_filename.c_str());
        return false;
    }

    return true;
}

bool HroConfig::validate(std::string& error_message) const
{
    auto set_error = [&](const std::string& message) {
        error_message = message;
        return false;
    };

    // Station position
    if (latitude < -90.0 || latitude > 90.0)
        return set_error(
            "Latitude must be between -90 and +90 degrees");

    if (longitude < -180.0 || longitude > 180.0)
        return set_error(
            "Longitude must be between -180 and +180 degrees");

    // Receiving frequency
    if (frequency_hz < 1000000UL ||
        frequency_hz > 2000000000UL)
    {
        return set_error(
            "Receiving frequency must be between 1 MHz and 2000 MHz");
    }

    // FFT settings
    if (fft_center_hz < 0)
        return set_error(
            "FFT center frequency must be 0 Hz or greater");

    const int64_t fft_min =
        static_cast<int64_t>(fft_center_hz) -
        static_cast<int64_t>(hro::FFT_RANGE_HZ);

    const int64_t fft_max =
        static_cast<int64_t>(fft_center_hz) +
        static_cast<int64_t>(hro::FFT_RANGE_HZ);

    if (fft_min < 0)
        return set_error(
            "FFT display lower frequency must be 0 Hz or greater");

    // Current resampler processing bandwidth
    if (fft_max > 1800)
        return set_error(
            "FFT display upper frequency must not exceed 1800 Hz");

    // RTL-SDR tuner gain
    if (std::find(
        hro::SDR_GAIN_VALUES.begin(),
        hro::SDR_GAIN_VALUES.end(),
        sdr_gain) == hro::SDR_GAIN_VALUES.end())
    {
        return set_error(
            "SDR gain is not supported by the RTL-SDR tuner");
    }

    // Level Peak Range: 0 ... FFT Range
    if (level_peak_range_hz < 0 ||
        level_peak_range_hz > hro::FFT_RANGE_HZ)
    {
        return set_error(
            "Level Peak Range must be between 0 and FFT Range");
    }

    // Screenshot prefix: 1-8 alphanumeric characters
    if (screenshot_prefix.empty() ||
        screenshot_prefix.size() > 8)
    {
        return set_error(
            "Screenshot File Prefix must be 1 to 8 alphanumeric characters");
    }

    for (char c : screenshot_prefix)
    {
        const bool valid =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9');

        if (!valid)
            return set_error(
                "Screenshot File Prefix must be 1 to 8 alphanumeric characters");
    }

    error_message.clear();
    return true;
}

void HroConfig::setDefaults()
{
    observer = "";
    location = "";
    latitude = 0.0;
    longitude = 0.0;

    receiver = "RTL-SDR Blog V4";
    frequency_hz = 53750000;
    sdr_gain = hro::DEFAULT_SDR_GAIN;
    fft_center_hz = 780;
    antenna = "";
    level_peak_range_hz = 5;

    screenshot_prefix = "HRO";
}
