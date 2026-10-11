#include "stick_visual.hpp"
#include "stick_audio.hpp"
#include "hro_visual_packet.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

namespace stick {
namespace {
constexpr const char* tag = "stick_visual";
struct Pending {
    hro::visual::Request request;
    bool busy = false;
    sockaddr_in server{};
    std::int64_t deadline = 0, next_send = 0, created_at = 0;
};
Pending queue[8];
int sock = -1;
const char* status = "A: VISUAL";
unsigned acknowledgements = 0, last_meteor_count = 1;
std::uint32_t event_sequence = 0;
char ack_status[32] = {};
unsigned count() { unsigned n = 0; for (const auto& item : queue) if (item.deadline) ++n; return n; }
}
const char* visual_status() {
    if (status == ack_status) {
        std::snprintf(ack_status, sizeof(ack_status), "ACK #%u x%u %s", acknowledgements, last_meteor_count, audio_ready() ? "OK" : "audio error");
    }
    return status;
}
void visual_send(const NetworkView& network, const char* stick_id, unsigned meteor_count) {
    if (network.selection_mode) return;
    if (event_sequence == UINT32_MAX) { status = "Count limit reached"; return; }
    const auto sequence = ++event_sequence;
    // Count every new input, including inputs that cannot be sent.
    if (!network.connected) {
        status = "Not sent: offline"; ESP_LOGW(tag, "%s", status); return;
    }
    for (auto& item : queue) {
        if (item.deadline) continue;
        item = {};
        item.request.version = 3; item.request.meteor_count = meteor_count; item.request.sequence = sequence;
        item.server.sin_family = AF_INET; item.server.sin_port = htons(hro::visual::PORT);
        if (inet_pton(AF_INET, network.gateway, &item.server.sin_addr) != 1) return;
        std::snprintf(item.request.stick_id, sizeof(item.request.stick_id), "%s", stick_id);
        std::snprintf(item.request.event_id, sizeof(item.request.event_id), "%08lX%08lX",
            static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()));
        item.created_at = esp_timer_get_time();
        item.deadline = item.created_at + 10000000;
        status = "Waiting ACK";
        return;
    }
    status = "Not sent: queue full"; ESP_LOGW(tag, "%s", status);
}
void visual_poll(const NetworkView& network) {
    const auto now = esp_timer_get_time();
    if (sock < 0 && count() && network.connected) sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        // Bound work per tick so Wi-Fi, buttons and clock remain responsive.
        for (unsigned i = 0; i < 8; ++i) {
            char data[80]; sockaddr_in peer{}; socklen_t size = sizeof(peer);
            const int n = recvfrom(sock, data, sizeof(data), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer), &size);
            if (n <= 0) break;
            for (auto& item : queue) {
                if (!item.deadline || now >= item.deadline || peer.sin_addr.s_addr != item.server.sin_addr.s_addr ||
                    peer.sin_port != item.server.sin_port) continue;
                if (hro::visual::matches_busy(data, n, item.request)) {
                    if (!item.busy) ESP_LOGW(tag, "Tab5 cache busy; Event=%s", item.request.event_id);
                    item.busy = true; status = "Tab5 BUSY";
                    continue;
                }
                if (!hro::visual::matches_ack(data, n, item.request)) continue;
                ESP_LOGI(tag, "ACK received; Stick=%s Event=%s delay=%lldms", item.request.stick_id, item.request.event_id,
                    static_cast<long long>((now - item.created_at) / 1000));
                item.deadline = 0;
                acknowledgements = item.request.sequence;
                last_meteor_count = item.request.meteor_count;
                status = count() ? "Waiting ACK" : ack_status;
                audio_ack(item.request.meteor_count);
                break;
            }
        }
    }
    for (auto& item : queue) {
        if (!item.deadline) continue;
        if (now >= item.deadline) {
            ESP_LOGW(tag, "ACK timeout; Event=%s (delivery unknown)", item.request.event_id);
            item.deadline = 0; status = item.busy ? "Tab5 BUSY timeout" : "ACK timeout"; continue;
        }
        in_addr gateway{};
        if (sock < 0 || !network.connected || now < item.next_send ||
            inet_pton(AF_INET, network.gateway, &gateway) != 1 || gateway.s_addr != item.server.sin_addr.s_addr) continue;
        char packet[64];
        const int length = hro::visual::encode(packet, sizeof(packet), item.request);
        if (length < 0) continue;
        sendto(sock, packet, length, MSG_DONTWAIT,
            reinterpret_cast<sockaddr*>(&item.server), sizeof(item.server));
        item.next_send = now + 500000; // Same ID; Tab5 deduplicates and ACKs again.
    }
    if (!count() && sock >= 0) { close(sock); sock = -1; }
}
}
