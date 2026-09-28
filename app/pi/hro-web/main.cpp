#include <httplib.h>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "hro_config.h"

#include <chrono>
#include <thread>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <deque>

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

    
class LiveWebSocketServer
{
public:
    using Server = websocketpp::server<websocketpp::config::asio>;

    static constexpr std::size_t MAX_LIVE_CLIENTS = 8;

    using ConnectionSet =
        std::set<
            websocketpp::connection_hdl,
            std::owner_less<websocketpp::connection_hdl>
        >;

    LiveWebSocketServer()
    {
        server_.clear_access_channels(
            websocketpp::log::alevel::all);

        server_.clear_error_channels(
            websocketpp::log::elevel::all);

        server_.init_asio();

        server_.set_open_handler(
            [this](websocketpp::connection_hdl hdl)
            {
                if (clients_.size() >= MAX_LIVE_CLIENTS)
                {
                    std::cout
                        << "LIVE client rejected: maximum "
                        << MAX_LIVE_CLIENTS
                        << " clients reached\n";

                    server_.close(
                        hdl,
                        websocketpp::close::status::try_again_later,
                        "Maximum LIVE clients reached"
                    );

                    return;
                }

                clients_.insert(hdl);

                // Send stored history to the newly connected client.
                for (const auto& data : history_)
                {
                    websocketpp::lib::error_code ec;

                    server_.send(
                        hdl,
                        data,
                        websocketpp::frame::opcode::text,
                        ec
                    );

                    if (ec)
                    {
                        std::cerr
                            << "LIVE history send error: "
                            << ec.message()
                            << "\n";
                        break;
                    }
                }                

                std::cout
                    << "LIVE client connected ("
                    << clients_.size()
                    << "/"
                    << MAX_LIVE_CLIENTS
                    << ")\n";
            });

        server_.set_close_handler(
            [this](websocketpp::connection_hdl hdl)
            {
                clients_.erase(hdl);

                std::cout
                    << "LIVE client disconnected ("
                    << clients_.size()
                    << "/"
                    << MAX_LIVE_CLIENTS
                    << ")\n";
            });
    }

    void sendTestData()
    {
        json message;

        message["type"] = "fft";
        message["timestamp"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

        message["sequence"] = sequence_++;

        // Temporary test FFT data: 501 bins
        constexpr int FFT_BIN_COUNT = 501;

        std::vector<float> fftBins;
        fftBins.reserve(FFT_BIN_COUNT);

        for (int i = 0; i < FFT_BIN_COUNT; ++i)
        {
            // Test pattern only:
            // peak around the center bin
            const float distance =
                std::abs(static_cast<float>(i - 250));

            const float value =
                std::max(-80.0f, -20.0f - distance * 0.25f);

            fftBins.push_back(value);
        }

        message["fft"] = fftBins;

        // Temporary test values
        message["level"] = -35.0;
        message["peak"]  = 10.0;

        const std::string data = message.dump();

        // Keep the latest 1200 seconds for newly connected clients.
        history_.push_back(data);

        while (history_.size() > LIVE_HISTORY_SECONDS)
        {
            history_.pop_front();
        }

        for (const auto& hdl : clients_)
        {
            websocketpp::lib::error_code ec;

            server_.send(
                hdl,
                data,
                websocketpp::frame::opcode::text,
                ec
            );

            if (ec)
            {
                std::cerr
                    << "LIVE send error: "
                    << ec.message()
                    << "\n";
            }
        }
    }

    void postTestData()
    {
        server_.get_io_service().post(
            [this]()
            {
                sendTestData();
            });
    }

    void run(uint16_t port)
    {
        websocketpp::lib::error_code ec;

        std::cout << "LIVE: listen(" << port << ")\n";

        server_.listen(port, ec);

        if (ec)
        {
            std::cerr
                << "LIVE listen error: "
                << ec.value()
                << " - "
                << ec.message()
                << "\n";
            return;
        }

        std::cout << "LIVE: start_accept()\n";

        server_.start_accept(ec);

        if (ec)
        {
            std::cerr
                << "LIVE start_accept error: "
                << ec.value()
                << " - "
                << ec.message()
                << "\n";
            return;
        }

        std::cout
            << "LIVE WebSocket listening on port "
            << port << "...\n";

        server_.run();
    }
private:
    static constexpr std::size_t LIVE_HISTORY_SECONDS = 1200;

    Server server_;
    ConnectionSet clients_;
    std::deque<std::string> history_;
    uint64_t sequence_ = 0;
};

int main()
{
    LiveWebSocketServer liveServer;

    std::thread liveThread(
        [&liveServer]()
        {
            liveServer.run(8081);
        });

    std::thread testThread(
        [&liveServer]()
        {
            while (true)
            {
                std::this_thread::sleep_for(
                    std::chrono::seconds(1));

                liveServer.postTestData();
            }
        });

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


