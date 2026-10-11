#include "stick_time.hpp"
#include "stick_network.hpp"
#include "hro_time_packet.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <ctime>

namespace stick {
namespace {
int sock = -1;
sockaddr_in server{};
char local_ip[16] = {};
char nonce[17] = {};
bool pending = false, synced = false;
std::int64_t sent_at = 0, next_request = 0, anchor_us = 0;
std::uint64_t anchor_ms = 0;
void disconnect() {
    if (sock >= 0) close(sock);
    sock = -1; pending = false; next_request = 0;
}
}
bool clock_synced() { return synced; }
void clock_text(char* out, std::size_t size) {
    if (!synced) { std::snprintf(out, size, "--:--:-- JST"); return; }
    const auto ms = anchor_ms + static_cast<std::uint64_t>((esp_timer_get_time() - anchor_us) / 1000);
    // Transport is UTC; only the display adds JST (+09:00).
    const time_t jst = static_cast<time_t>(ms / 1000 + 9 * 3600);
    tm time{}; gmtime_r(&jst, &time);
    std::strftime(out, size, "%H:%M:%S JST", &time);
}
void clock_poll(const NetworkView& network) {
    if (!network.connected) { disconnect(); return; }
    in_addr address{};
    if (inet_pton(AF_INET, network.gateway, &address) != 1 || !address.s_addr) return;
    if (sock >= 0 && (server.sin_addr.s_addr != address.s_addr || std::strcmp(local_ip, network.ip_address))) disconnect();
    const auto now = esp_timer_get_time();
    if (sock < 0) {
        if (now < next_request) return;
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) { next_request = now + 5000000; return; }
        server = {}; server.sin_family = AF_INET; server.sin_port = htons(hro::clock::PORT); server.sin_addr = address;
        std::snprintf(local_ip, sizeof(local_ip), "%s", network.ip_address);
    }
    if (pending) {
        char data[80]; sockaddr_in peer{}; socklen_t size = sizeof(peer);
        const int n = recvfrom(sock, data, sizeof(data), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer), &size);
        std::uint64_t ms = 0;
        if (n > 0 && peer.sin_addr.s_addr == server.sin_addr.s_addr && peer.sin_port == server.sin_port &&
            hro::clock::parse(data, static_cast<std::size_t>(n), nonce, ms)) {
            const auto rtt_us = now - sent_at;
            pending = false;
            if (ms && rtt_us <= 2000000) {
                anchor_ms = ms + static_cast<std::uint64_t>(rtt_us / 2000);
                anchor_us = now;
                if (!synced) ESP_LOGI("stick_time", "Clock received from Tab5 (UTC); display JST");
                synced = true;
                next_request = now + 60000000;
            } else {
                if (!ms) synced = false;
                next_request = now + 5000000;
            }
        } else if (now - sent_at >= 2000000) {
            pending = false; next_request = now + 5000000;
        }
    }
    if (!pending && now >= next_request) {
        std::snprintf(nonce, sizeof(nonce), "%08lX%08lX", static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
        char request[24]; std::snprintf(request, sizeof(request), "TIME 1 %s", nonce);
        if (sendto(sock, request, 23, 0, reinterpret_cast<sockaddr*>(&server), sizeof(server)) == 23) {
            pending = true; sent_at = now;
        } else next_request = now + 5000000;
    }
}
}
