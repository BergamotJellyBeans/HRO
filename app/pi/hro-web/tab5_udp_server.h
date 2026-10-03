#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <netinet/in.h>

// Registers display terminals without changing the engine's local UDP outputs.
class Tab5UdpServer
{
public:
    explicit Tab5UdpServer(const char* configPath = "/etc/hro/config.ini");
    ~Tab5UdpServer();
    bool start();
    void forward(const uint8_t* data, std::size_t size, bool audio);

private:
    struct Client {
        sockaddr_in dataAddress{};
        sockaddr_in audioAddress{};
        std::chrono::steady_clock::time_point expires;
    };
    void receiveRequests();
    void expireClients(); // Called with clientsMutex_ held.

    const char* configPath_;
    int controlSocket_ = -1;
    int sendSocket_ = -1;
    std::atomic<bool> running_{false};
    std::thread controlThread_;
    std::mutex clientsMutex_;
    std::map<uint32_t, Client> clients_;
};
