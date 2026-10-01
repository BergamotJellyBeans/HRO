#include <httplib.h>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "hro_config.h"
#include "hro_fft_config.h"

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
#include <cstdint>
#include <cstring>
#include <filesystem>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <utility>

using json = nlohmann::json;

namespace
{
constexpr uint32_t HRO_LIVE_MAGIC = 0x48524F31;  // "HRO1"
constexpr uint16_t HRO_LIVE_VERSION = 1;
//constexpr std::size_t HRO_LIVE_FFT_BINS = 601;
constexpr std::size_t HRO_LIVE_FFT_BINS = hro::FFT_BIN_COUNT;
constexpr uint16_t HRO_LIVE_UDP_PORT = 50000;

constexpr std::size_t HRO_LIVE_HEADER_SIZE = 28;
constexpr std::size_t HRO_LIVE_PACKET_SIZE =
    HRO_LIVE_HEADER_SIZE +
    HRO_LIVE_FFT_BINS * sizeof(float);

static_assert(
    HRO_LIVE_PACKET_SIZE == 2432,   // 2032->2432
    "Unexpected HRO Live UDP packet size"
);

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

uint16_t readUint16BE(const uint8_t* p)
{
    return
        (static_cast<uint16_t>(p[0]) << 8) |
         static_cast<uint16_t>(p[1]);
}

uint32_t readUint32BE(const uint8_t* p)
{
    return
        (static_cast<uint32_t>(p[0]) << 24) |
        (static_cast<uint32_t>(p[1]) << 16) |
        (static_cast<uint32_t>(p[2]) << 8)  |
         static_cast<uint32_t>(p[3]);
}

uint64_t readUint64BE(const uint8_t* p)
{
    uint64_t value = 0;

    for (int i = 0; i < 8; ++i)
    {
        value =
            (value << 8) |
            static_cast<uint64_t>(p[i]);
    }

    return value;
}

float readFloat32BE(const uint8_t* p)
{
    const uint32_t bits = readUint32BE(p);

    float value;
    static_assert(sizeof(value) == sizeof(bits));

    std::memcpy(&value, &bits, sizeof(value));

    return value;
}

struct HroLiveData
{
    uint64_t sequence = 0;
    int64_t timestamp_ms = 0;
    float peak_db = 0.0f;
    std::vector<float> fft_db;
};

bool decodeHroLivePacket(
    const uint8_t* data,
    std::size_t size,
    HroLiveData& output)
{
    if (size != HRO_LIVE_PACKET_SIZE)
    {
        return false;
    }

    const uint32_t magic =
        readUint32BE(data + 0);

    const uint16_t version =
        readUint16BE(data + 4);

    const uint16_t binCount =
        readUint16BE(data + 6);

    if (magic != HRO_LIVE_MAGIC ||
        version != HRO_LIVE_VERSION ||
        binCount != HRO_LIVE_FFT_BINS)
    {
        return false;
    }

    output.sequence =
        readUint64BE(data + 8);

    output.timestamp_ms =
        static_cast<int64_t>(
            readUint64BE(data + 16));

    output.peak_db =
        readFloat32BE(data + 24);

    output.fft_db.resize(HRO_LIVE_FFT_BINS);

    for (std::size_t i = 0;
         i < HRO_LIVE_FFT_BINS;
         ++i)
    {
        output.fft_db[i] =
            readFloat32BE(
                data + HRO_LIVE_HEADER_SIZE +
                i * sizeof(float));
    }

    return true;
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

    void sendLiveData(
        uint64_t sequence,
        int64_t timestampMs,
        const std::vector<float>& fftBins,
        float peakDb)
    {
        json message;

        message["type"] = "fft";
        message["timestamp"] = timestampMs;
        message["sequence"] = sequence;
        message["fft"] = fftBins;
        message["peak"] = peakDb;

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

    void sendTestData()
    {
    constexpr int FFT_BIN_COUNT =
        static_cast<int>(hro::FFT_BIN_COUNT);
        
        std::vector<float> fftBins;
        fftBins.reserve(FFT_BIN_COUNT);

        for (int i = 0; i < FFT_BIN_COUNT; ++i)
        {
            const float distance =
                std::abs(static_cast<float>(i - 300));  // 250->300

            const float value =
                std::max(
                    -80.0f,
                    -20.0f - distance * 0.25f
                );

            fftBins.push_back(value);
        }

        const int64_t timestampMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

        sendLiveData(
            sequence_++,
            timestampMs,
            fftBins,
            10.0f
        );
    }

    void postLiveData(
        uint64_t sequence,
        int64_t timestampMs,
        std::vector<float> fftBins,
        float peakDb)
    {
        server_.get_io_service().post(
            [this,
            sequence,
            timestampMs,
            fftBins = std::move(fftBins),
            peakDb]() mutable
            {
                sendLiveData(
                    sequence,
                    timestampMs,
                    fftBins,
                    peakDb
                );
            });
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

        // Allow immediate restart while previous WebSocket connections remain in TIME_WAIT.
        server_.set_reuse_addr(true);

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

class AudioWebSocketServer
{
public:
    using Server =
        websocketpp::server<websocketpp::config::asio>;

    static constexpr std::size_t MAX_AUDIO_CLIENTS = 8;

    using ConnectionSet =
        std::set<
            websocketpp::connection_hdl,
            std::owner_less<websocketpp::connection_hdl>
        >;

    AudioWebSocketServer()
    {
        server_.clear_access_channels(
            websocketpp::log::alevel::all);

        server_.clear_error_channels(
            websocketpp::log::elevel::all);

        server_.init_asio();

        server_.set_open_handler(
            [this](websocketpp::connection_hdl hdl)
            {
                if (clients_.size() >= MAX_AUDIO_CLIENTS)
                {
                    server_.close(
                        hdl,
                        websocketpp::close::status::try_again_later,
                        "Maximum AUDIO clients reached"
                    );

                    return;
                }

                clients_.insert(hdl);

                std::cout
                    << "AUDIO client connected ("
                    << clients_.size()
                    << "/"
                    << MAX_AUDIO_CLIENTS
                    << ")\n";
            });

        server_.set_close_handler(
            [this](websocketpp::connection_hdl hdl)
            {
                clients_.erase(hdl);

                std::cout
                    << "AUDIO client disconnected ("
                    << clients_.size()
                    << "/"
                    << MAX_AUDIO_CLIENTS
                    << ")\n";
            });
    }

    void sendAudio(
        const std::vector<uint8_t>& data)
    {
        for (const auto& hdl : clients_)
        {
            websocketpp::lib::error_code ec;

            server_.send(
                hdl,
                data.data(),
                data.size(),
                websocketpp::frame::opcode::binary,
                ec
            );

            if (ec)
            {
                std::cerr
                    << "AUDIO send error: "
                    << ec.message()
                    << '\n';
            }
        }
    }

    void postAudio(
        std::vector<uint8_t> data)
    {
        server_.get_io_service().post(
            [this,
             data = std::move(data)]() mutable
            {
                sendAudio(data);
            });
    }

    void run(uint16_t port)
    {
        websocketpp::lib::error_code ec;

        server_.set_reuse_addr(true);

        std::cout
            << "AUDIO: listen("
            << port
            << ")\n";

        server_.listen(port, ec);

        if (ec)
        {
            std::cerr
                << "AUDIO listen error: "
                << ec.value()
                << " - "
                << ec.message()
                << '\n';

            return;
        }

        server_.start_accept(ec);

        if (ec)
        {
            std::cerr
                << "AUDIO start_accept error: "
                << ec.value()
                << " - "
                << ec.message()
                << '\n';

            return;
        }

        std::cout
            << "AUDIO WebSocket listening on port "
            << port
            << "...\n";

        server_.run();
    }

private:
    Server server_;
    ConnectionSet clients_;
};

void runUdpReceiver(LiveWebSocketServer& liveServer)
{
    const int sock =
        ::socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        std::cerr
            << "UDP socket creation failed\n";
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(HRO_LIVE_UDP_PORT);

    if (::bind(
            sock,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        std::cerr
            << "UDP bind failed on port "
            << HRO_LIVE_UDP_PORT
            << "\n";

        ::close(sock);
        return;
    }

    std::cout
        << "LIVE UDP listening on port "
        << HRO_LIVE_UDP_PORT
        << "...\n";

    std::vector<uint8_t> buffer(
        HRO_LIVE_PACKET_SIZE);

    while (true)
    {
        const ssize_t received =
            ::recvfrom(
                sock,
                buffer.data(),
                buffer.size(),
                0,
                nullptr,
                nullptr);

        if (received < 0)
        {
            std::cerr
                << "UDP receive error\n";
            continue;
        }

        HroLiveData liveData;

        if (!decodeHroLivePacket(
                buffer.data(),
                static_cast<std::size_t>(received),
                liveData))
        {
            std::cerr
                << "Invalid HRO LIVE UDP packet: "
                << received
                << " bytes\n";
            continue;
        }

        liveServer.postLiveData(
            liveData.sequence,
            liveData.timestamp_ms,
            std::move(liveData.fft_db),
            liveData.peak_db);
    }

    ::close(sock);
}

void runAudioUdpReceiver(AudioWebSocketServer& audioServer)
{
    constexpr uint16_t HRO_AUDIO_UDP_PORT = 50002;
    constexpr std::size_t AUDIO_PACKET_SIZE =
        256 * sizeof(float);

    const int sock =
        ::socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        std::cerr
            << "AUDIO UDP socket creation failed\n";
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(HRO_AUDIO_UDP_PORT);

    if (::bind(
            sock,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0)
    {
        std::cerr
            << "AUDIO UDP bind failed on port "
            << HRO_AUDIO_UDP_PORT
            << "\n";

        ::close(sock);
        return;
    }

    std::cout
        << "AUDIO UDP listening on port "
        << HRO_AUDIO_UDP_PORT
        << "...\n";

    std::vector<uint8_t> buffer(
        AUDIO_PACKET_SIZE);

    while (true)
    {
        const ssize_t received =
            ::recvfrom(
                sock,
                buffer.data(),
                buffer.size(),
                0,
                nullptr,
                nullptr);

        if (received < 0)
        {
            std::cerr
                << "AUDIO UDP receive error\n";
            continue;
        }

        if (received !=
            static_cast<ssize_t>(AUDIO_PACKET_SIZE))
        {
            std::cerr
                << "Invalid AUDIO UDP packet: "
                << received
                << " bytes\n";
            continue;
        }

        std::vector<uint8_t> audioData(
            buffer.begin(),
            buffer.begin() + received);

        audioServer.postAudio(
            std::move(audioData));
    }

    ::close(sock);
}

int main()
{
    LiveWebSocketServer liveServer;
    AudioWebSocketServer audioServer;

    std::thread liveThread(
        [&liveServer]()
        {
            liveServer.run(8081);
        });

    std::thread udpThread(
        [&liveServer]()
        {
            runUdpReceiver(liveServer);
        });

    std::thread audioThread(
        [&audioServer]()
        {
            audioServer.run(8082);
        });

    std::thread audioUdpThread(
        [&audioServer]()
        {
            runAudioUdpReceiver(audioServer);
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

    server.Get("/archive",
        [](const httplib::Request&, httplib::Response& res)
        {
            res.set_file_content(
                "ui/web/archive.html",
                "text/html");
        });

    server.Get("/assets/radio_meteor_observation_base_1280x720.png",
        [](const httplib::Request&, httplib::Response& res)
        {
            std::ifstream file(
                "ui/assets/radio_meteor_observation_base_1280x720.png",
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

    server.Get(R"(/archive/image/(.+\.png))",
        [](const httplib::Request& req, httplib::Response& res)
        {
            namespace fs = std::filesystem;

            const std::string filename = req.matches[1];

            // ファイル名だけを許可する
            if (filename.find('/') != std::string::npos ||
                filename.find('\\') != std::string::npos ||
                filename.find("..") != std::string::npos)
            {
                res.status = 400;
                res.set_content("Invalid filename\n", "text/plain");
                return;
            }

            const fs::path archiveRoot = "/mnt/hro/png";
            fs::path foundPath;

            try
            {
                for (const auto& entry :
                     fs::recursive_directory_iterator(archiveRoot))
                {
                    if (!entry.is_regular_file())
                        continue;

                    if (entry.path().filename() == filename)
                    {
                        foundPath = entry.path();
                        break;
                    }
                }

                if (foundPath.empty())
                {
                    res.status = 404;
                    res.set_content("Archive image not found\n", "text/plain");
                    return;
                }

                res.set_file_content(
                    foundPath.string(),
                    "image/png");
            }
            catch (const std::exception& e)
            {
                res.status = 500;
                res.set_content(e.what(), "text/plain");
            }
        });

    server.Get(R"(/archive/data/(.+\.json))",
        [](const httplib::Request& req, httplib::Response& res)
        {
            namespace fs = std::filesystem;

            const std::string filename = req.matches[1];

            // ファイル名以外を受け付けない
            if (filename.find('/') != std::string::npos ||
                filename.find('\\') != std::string::npos ||
                filename.find("..") != std::string::npos)
            {
                res.status = 400;
                res.set_content(
                    "Invalid filename\n",
                    "text/plain");
                return;
            }

            const fs::path archiveRoot = "/mnt/hro/png";
            fs::path foundPath;

            try
            {
                for (const auto& entry :
                     fs::recursive_directory_iterator(archiveRoot))
                {
                    if (!entry.is_regular_file())
                        continue;

                    if (entry.path().filename() == filename)
                    {
                        foundPath = entry.path();
                        break;
                    }
                }
            

                if (foundPath.empty())
                {
                    res.status = 404;
                    res.set_content(
                        "Archive data not found\n",
                        "text/plain");
                    return;
                }

                res.set_file_content(
                    foundPath.string(),
                    "application/json");
            }
            catch (const std::exception& e)
            {
                res.status = 500;
                res.set_content(
                    e.what(),
                    "text/plain");
            }
        });

    server.Get("/api/archive/latest",
        [](const httplib::Request&, httplib::Response& res)
        {
            namespace fs = std::filesystem;

            const fs::path archiveRoot = "/mnt/hro/png";

            fs::path latestPng;
            std::string latestName;

            try
            {
                if (!fs::exists(archiveRoot))
                {
                    res.status = 404;
                    res.set_content(
                        "{\"error\":\"Archive directory not found\"}\n",
                        "application/json");
                    return;
                }

                for (const auto& entry :
                     fs::recursive_directory_iterator(archiveRoot))
                {
                    if (!entry.is_regular_file())
                        continue;

                    const fs::path& path = entry.path();

                    if (path.extension() != ".png")
                        continue;

                    const std::string name =
                        path.filename().string();
                    
                    if (name.rfind("._", 0) == 0)
                        continue;

                    // Example:
                    // BJ202610010800.png
                    //
                    // Filename ordering is chronological because
                    // YYYYMMDDHHMM is embedded in the name.
                    if (latestName.empty() || name > latestName)
                    {
                        latestName = name;
                        latestPng = path;
                    }
                }

                if (latestName.empty())
                {
                    res.status = 404;
                    res.set_content(
                        "{\"error\":\"No archive PNG found\"}\n",
                        "application/json");
                    return;
                }

                fs::path latestJson = latestPng;
                latestJson.replace_extension(".json");

                json result;

                result["png_file"] =
                    latestPng.filename().string();

                result["json_file"] =
                    latestJson.filename().string();

                result["png_path"] =
                    latestPng.string();

                result["json_path"] =
                    latestJson.string();

                result["json_exists"] =
                    fs::exists(latestJson);

                res.set_content(
                    result.dump(2) + "\n",
                    "application/json");
            }
            catch (const std::exception& e)
            {
                json result;
                result["error"] = e.what();

                res.status = 500;
                res.set_content(
                    result.dump(2) + "\n",
                    "application/json");
            }
        });

    server.Get("/api/archive/list",
        [](const httplib::Request&, httplib::Response& res)
        {
            namespace fs = std::filesystem;

            const fs::path archiveRoot = "/mnt/hro/png";

            try
            {
                std::vector<fs::path> files;

                if (!fs::exists(archiveRoot))
                {
                    res.status = 404;
                    res.set_content(
                        "{\"error\":\"Archive directory not found\"}\n",
                        "application/json");
                    return;
                }

                for (const auto& entry :
                     fs::recursive_directory_iterator(archiveRoot))
                {
                    if (!entry.is_regular_file())
                        continue;

                    const fs::path& path = entry.path();

                if (path.extension() != ".png")
                    continue;

                const std::string filename =
                    path.filename().string();

                // macOS AppleDouble file は除外
                if (filename.rfind("._", 0) == 0)
                    continue;

                files.push_back(path);
             }

                std::sort(
                    files.begin(),
                    files.end(),
                    [](const fs::path& a, const fs::path& b)
                    {
                        return a.filename().string() <
                               b.filename().string();
                    });

                json result = json::array();

                for (const auto& path : files)
                {
                    fs::path jsonPath = path;
                    jsonPath.replace_extension(".json");

                    // JSONが存在する新形式データは、
                    // 完全な20分ブロックだけArchiveに掲載する。
                    // JSONがない旧データはそのまま掲載する。
                    if (fs::exists(jsonPath))
                    {
                        std::ifstream jsonFile(jsonPath);

                        if (jsonFile)
                        {
                            json metadata;
                            jsonFile >> metadata;

                            if (metadata.contains("data_quality"))
                            {
                                const auto& quality =
                                    metadata["data_quality"];

                                const int expected =
                                    quality.value(
                                        "expected_seconds", 0);

                                const int received =
                                    quality.value(
                                        "received_seconds", 0);

                                if (expected > 0 &&
                                    received < expected)
                                {
                                    continue;
                                }
                            }
                        }
                    }
                
                    json item;

                    item["png_file"] =
                        path.filename().string();

                    item["json_file"] =
                        jsonPath.filename().string();

                    item["json_exists"] =
                        fs::exists(jsonPath);

                    result.push_back(item);
                }

                res.set_content(
                    result.dump(2) + "\n",
                    "application/json");
            }
            catch (const std::exception& e)
            {
                json result;
                result["error"] = e.what();

                res.status = 500;
                res.set_content(
                    result.dump(2) + "\n",
                    "application/json");
            }
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
                << "    \"fft_range_hz\": " << hro::FFT_RANGE_HZ << ",\n"
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


