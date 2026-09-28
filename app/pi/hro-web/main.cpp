#include <httplib.h>

#include "hro_config.h"

#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

    server.Get("/monitor",
        [](const httplib::Request&, httplib::Response& res)
        {
            std::ifstream file("ui/web/monitor.html");

            if (!file.is_open())
            {
                res.status = 404;
                res.set_content("monitor.html not found\n", "text/plain");
                return;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();

            res.set_content(buffer.str(), "text/html");
        });

    server.Get("/assets/radio_meteor_observation_base.png",
        [](const httplib::Request&, httplib::Response& res)
        {
            std::ifstream file(
                "ui/assets/radio_meteor_observation_base.png",
                std::ios::binary);

            if (!file.is_open())
            {
                res.status = 404;
                res.set_content("base image not found\n", "text/plain");
                return;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();

            res.set_content(buffer.str(), "image/png");
        });

    server.Get("/assets/fonts/NunitoSans-VariableFont.ttf",
        [](const httplib::Request&, httplib::Response& res)
        {
            std::ifstream file(
                "ui/assets/fonts/NunitoSans-VariableFont.ttf",
                std::ios::binary);

            if (!file.is_open())
            {
                res.status = 404;
                res.set_content("font not found\n", "text/plain");
                return;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();

            res.set_content(buffer.str(), "font/ttf");
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

                << "  \"screenshot\": {\n"
                << "    \"prefix\": \""
                << jsonEscape(config.screenshot_prefix) << "\"\n"
                << "  }\n"
                << "}\n";

            res.set_content(json.str(), "application/json");
        });

    server.Post("/api/config",
        [](const httplib::Request& req, httplib::Response& res)
        {
            try
            {
                const json body = json::parse(req.body);

                HroConfig config;

                config.observer =
                    body.at("station").at("observer").get<std::string>();

                config.location =
                    body.at("station").at("location").get<std::string>();

                config.latitude =
                    body.at("station").at("latitude").get<double>();

                config.longitude =
                    body.at("station").at("longitude").get<double>();

                config.receiver =
                    body.at("receiver").at("receiver").get<std::string>();

                config.frequency_hz =
                    body.at("receiver").at("frequency_hz").get<uint32_t>();

                config.fft_center_hz =
                    body.at("receiver").at("fft_center_hz").get<int>();

                config.fft_range_hz =
                    body.at("receiver").at("fft_range_hz").get<int>();

                config.level_peak_range_hz =
                    body.at("receiver").at("level_peak_range_hz").get<int>();

                config.antenna =
                    body.at("receiver").at("antenna").get<std::string>();

                config.screenshot_prefix =
                    body.at("screenshot").at("prefix").get<std::string>();

                std::string error_message;

                if (!config.validate(error_message))
                {
                    res.status = 400;

                    json response;
                    response["error"] = error_message;

                    res.set_content(
                        response.dump() + "\n",
                        "application/json");
                    return;
                }

                if (!config.save("/etc/hro/config.ini"))
                {
                    res.status = 500;

                    res.set_content(
                        "{\"error\":\"Failed to save config.ini\"}\n",
                        "application/json");
                    return;
                }

                res.set_content(
                    "{\"status\":\"saved\"}\n",
                    "application/json");
       }
        catch (const std::exception& e)
        {
            res.status = 400;

            json response;
            response["error"] = e.what();

            res.set_content(
                response.dump() + "\n",
                "application/json");
        }
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
