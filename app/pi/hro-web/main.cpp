#include <httplib.h>

#include "hro_config.h"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{

std::string jsonEscape(const std::string& s)
{
    std::string out;

    for (const char c : s)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }

    return out;
}

} // namespace

int main()
{
    httplib::Server server;

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/settings");
    });

    server.Get("/settings",
        [](const httplib::Request&, httplib::Response& res)
        {
            res.set_file_content(
                "ui/web/settings.html",
                "text/html");
        });

    server.Get("/api/config",
        [](const httplib::Request&, httplib::Response& res)
        {
            HroConfig config;

            if (!config.load("/etc/hro/config.ini"))
            {
                res.status = 500;
                res.set_content(
                    "{\"error\":\"Failed to load config.ini\"}\n",
                    "application/json");
                return;
            }

            std::ostringstream json;

            json << std::setprecision(10);

            json
                << "{\n"
                << "  \"station\": {\n"
                << "    \"observer\": \"" << jsonEscape(config.observer) << "\",\n"
                << "    \"location\": \"" << jsonEscape(config.location) << "\",\n"
                << "    \"latitude\": " << config.latitude << ",\n"
                << "    \"longitude\": " << config.longitude << "\n"
                << "  },\n"

                << "  \"receiver\": {\n"
                << "    \"receiver\": \"" << jsonEscape(config.receiver) << "\",\n"
                << "    \"frequency_hz\": " << config.frequency_hz << ",\n"
                << "    \"fft_center_hz\": " << config.fft_center_hz << ",\n"
                << "    \"fft_range_hz\": " << config.fft_range_hz << ",\n"
                << "    \"level_peak_range_hz\": "
                << config.level_peak_range_hz << ",\n"
                << "    \"antenna\": \"" << jsonEscape(config.antenna) << "\"\n"
                << "  },\n"

                << "  \"audio\": {\n"
                << "    \"volume\": " << config.volume << ",\n"
                << "    \"mute\": "
                << (config.mute ? "true" : "false") << "\n"
                << "  },\n"

                << "  \"screenshot\": {\n"
                << "    \"prefix\": \""
                << jsonEscape(config.screenshot_prefix) << "\"\n"
                << "  }\n"
                << "}\n";

            res.set_content(json.str(), "application/json");
        });

    std::cout << "Pi5-HRO Web Server\n";
    std::cout << "Listening on port 8080...\n";

    if (!server.listen("0.0.0.0", 8080))
    {
        std::cerr << "Failed to start web server\n";
        return 1;
    }

    return 0;
}
