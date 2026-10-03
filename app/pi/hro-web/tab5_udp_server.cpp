#include "tab5_udp_server.h"
#include "hro_config.h"
#include "hro_fft_config.h"
#include "hro_version.h"

#include <nlohmann/json.hpp>
#include <array>
#include <cerrno>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr uint16_t CONTROL_PORT = 50003;
constexpr int LEASE_SECONDS = 15;
constexpr std::size_t MAX_CLIENTS = 8;
constexpr std::size_t MAX_CONTROL_PACKET = 4096;
using json = nlohmann::json;

json displayConfig(const HroConfig& c)
{
    return {
        {"system", {{"display_text", std::string(hro::SOFTWARE_NAME) + "   v" +
            hro::SOFTWARE_VERSION + "   Raspberry Pi 5 + RTL-SDR"}}},
        {"station", {{"observer", c.observer}, {"location", c.location},
                     {"latitude", c.latitude}, {"longitude", c.longitude}}},
        {"receiver", {{"receiver", c.receiver}, {"frequency_hz", c.frequency_hz},
                      {"sdr_gain", c.sdr_gain}, {"fft_center_hz", c.fft_center_hz},
                      {"fft_range_hz", hro::FFT_RANGE_HZ},
                      {"level_peak_range_hz", c.level_peak_range_hz},
                      {"antenna", c.antenna}}},
        {"screenshot", {{"prefix", c.screenshot_prefix}}}
    };
}
}

Tab5UdpServer::Tab5UdpServer(const char* configPath) : configPath_(configPath) {}

Tab5UdpServer::~Tab5UdpServer()
{
    running_.store(false);
    if (controlThread_.joinable()) controlThread_.join();
    if (controlSocket_ >= 0) ::close(controlSocket_);
    if (sendSocket_ >= 0) ::close(sendSocket_);
}

bool Tab5UdpServer::start()
{
    if (running_.load()) return true;
    controlSocket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    sendSocket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    auto fail = [this]() {
        if (controlSocket_ >= 0) ::close(controlSocket_);
        if (sendSocket_ >= 0) ::close(sendSocket_);
        controlSocket_ = sendSocket_ = -1;
        std::cerr << "Tab5 UDP server initialization failed\n";
        return false;
    };
    if (controlSocket_ < 0 || sendSocket_ < 0) return fail();
    timeval timeout{};
    timeout.tv_usec = 250000;
    if (::setsockopt(controlSocket_, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout)) < 0) return fail();
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(CONTROL_PORT);
    if (::bind(controlSocket_, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0) return fail();
    running_.store(true);
    controlThread_ = std::thread(&Tab5UdpServer::receiveRequests, this);
    std::cout << "Tab5 registration listening on UDP " << CONTROL_PORT << '\n';
    return true;
}

void Tab5UdpServer::expireClients()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (it->second.expires <= now) it = clients_.erase(it);
        else ++it;
    }
}

void Tab5UdpServer::forward(const uint8_t* data, std::size_t size, bool audio)
{
    if (!running_.load()) return;
    std::array<sockaddr_in, MAX_CLIENTS> destinations{};
    std::size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        expireClients();
        for (const auto& item : clients_) {
            destinations[count++] = audio ? item.second.audioAddress : item.second.dataAddress;
        }
    }
    // Never let a slow/unreachable terminal block local processing.
    for (std::size_t i = 0; i < count; ++i) {
        ::sendto(sendSocket_, data, size, MSG_DONTWAIT,
                 reinterpret_cast<const sockaddr*>(&destinations[i]), sizeof(sockaddr_in));
    }
}

void Tab5UdpServer::receiveRequests()
{
    std::array<char, MAX_CONTROL_PACKET + 1> buffer{};
    while (running_.load()) {
        sockaddr_in peer{};
        socklen_t peerSize = sizeof(peer);
        const auto size = ::recvfrom(controlSocket_, buffer.data(), buffer.size(), 0,
                                      reinterpret_cast<sockaddr*>(&peer), &peerSize);
        if (size < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                std::cerr << "Tab5 registration receive failed\n";
            continue;
        }
        if (size == 0 || size > static_cast<ssize_t>(MAX_CONTROL_PACKET)) continue;
        json response{{"type", "register_error"}, {"version", 1}};
        try {
            const auto request = json::parse(buffer.data(), buffer.data() + size);
            if (!request.is_object() || !request.contains("request_id") ||
                !request["request_id"].is_number_unsigned()) continue;
            response["request_id"] = request["request_id"];
            if (request.value("type", "") != "register" ||
                request.value("version", 0) != 1 ||
                request.value("device", "") != "Tab5-HRO" ||
                request.value("data_port", 0) != 50000 ||
                request.value("audio_port", 0) != 50002) {
                response["error"] = "invalid_request";
            } else {
                HroConfig config;
                std::string error;
                if (!config.load(configPath_) || !config.validate(error)) {
                    response["error"] = "config_unavailable";
                } else {
                    std::lock_guard<std::mutex> lock(clientsMutex_);
                    expireClients();
                    const auto key = peer.sin_addr.s_addr;
                    if (clients_.count(key) == 0 && clients_.size() >= MAX_CLIENTS) {
                        response["error"] = "server_full";
                    } else {
                        response["type"] = "register_ack";
                        response["lease_seconds"] = LEASE_SECONDS;
                        response["config"] = displayConfig(config);
                        const auto packet = response.dump();
                        if (packet.size() > MAX_CONTROL_PACKET) {
                            response.erase("config");
                            response["type"] = "register_error";
                            response["error"] = "config_too_large";
                        } else if (::sendto(controlSocket_, packet.data(), packet.size(),
                                            MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer),
                                            peerSize) == static_cast<ssize_t>(packet.size())) {
                            Client client;
                            client.dataAddress = client.audioAddress = peer;
                            client.dataAddress.sin_port = htons(50000);
                            client.audioAddress.sin_port = htons(50002);
                            client.expires = std::chrono::steady_clock::now() +
                                             std::chrono::seconds(LEASE_SECONDS);
                            clients_[key] = client;
                            continue; // ACK was sent before enabling forwarding.
                        } else {
                            continue;
                        }
                    }
                }
            }
        } catch (const json::exception&) {
            response["error"] = "invalid_request";
        }
        const auto packet = response.dump();
        ::sendto(controlSocket_, packet.data(), packet.size(), MSG_DONTWAIT,
                 reinterpret_cast<sockaddr*>(&peer), peerSize);
    }
}
