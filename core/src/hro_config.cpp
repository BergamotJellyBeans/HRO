#include "hro_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

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
    std::ifstream file(filename);
    if (!file)
        return false;

    std::string section;
    std::string line;

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
            continue;

        const std::string key   = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));

        try
        {
            if (section == "station")
            {
                if (key == "observer")
                    observer = value;
                else if (key == "location")
                    location = value;
                else if (key == "latitude")
                    latitude = std::stod(value);
                else if (key == "longitude")
                    longitude = std::stod(value);
            }
            else if (section == "receiver")
            {
                if (key == "receiver")
                    receiver = value;
                else if (key == "frequency_hz")
                    frequency_hz =
                        static_cast<uint32_t>(std::stoul(value));
                else if (key == "fft_center_hz")
                    fft_center_hz = std::stoi(value);
                else if (key == "fft_range_hz")
                    fft_range_hz = std::stoi(value);
                else if (key == "antenna")
                    antenna = value;
                else if (key == "level_peak_range_hz")
                    level_peak_range_hz = std::stoi(value);
            }
            else if (section == "audio")
            {
                if (key == "volume")
                    volume = std::stoi(value);
                else if (key == "mute")
                    mute = (std::stoi(value) != 0);
            }
            else if (section == "screenshot")
            {
                if (key == "prefix")
                    screenshot_prefix = value;
            }
        }
        catch (...)
        {
            return false;
        }
    }

    return true;
}
