#include "tab5_visual.hpp"
#include "tab5_visual_log.hpp"
#include "tab5_visual_markers.hpp"
#include "hro_time_packet.h"
#include "tab5_visual_cache.hpp"
#include "tab5_console.hpp"
#include "tab5_runtime.hpp"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <sys/time.h>

namespace hro::tab5::app {
namespace {
constexpr const char* visual_tag = "Tab5-VISUAL";
VisualCache cache; // Owned only by the receiver task, never on its stack.
TaskHandle_t receiver = nullptr;
std::uint64_t utc_ms() {
    timeval now{};
    if (gettimeofday(&now, nullptr) != 0 || now.tv_sec < 1577836800) return 0;
    return static_cast<std::uint64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
}
void receive_task(void*) {
    int sock = -1;
    while (!g_hro_shutdown_requested.load(std::memory_order_acquire)) {
        auto* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_ip_info_t ip{};
        if (!ap || esp_netif_get_ip_info(ap, &ip) != ESP_OK || !ip.ip.addr) {
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(hro::visual::PORT);
        address.sin_addr.s_addr = ip.ip.addr;
        timeval timeout{1, 0};
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
            bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            ESP_LOGW(visual_tag, "SoftAP UDP bind failed: errno=%d", errno);
            close(sock); sock = -1;
            vTaskDelay(pdMS_TO_TICKS(1000)); continue;
        }
        ESP_LOGI(visual_tag, "SoftAP UDP %u ready", hro::visual::PORT);
        console_message("Stick VISUAL UDP 50003 ready");
        while (!g_hro_shutdown_requested.load(std::memory_order_acquire)) {
            // Buffer is larger than either valid request; oversized packets fail parsing.
            char buffer[80];
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            const int length = recvfrom(sock, buffer, sizeof(buffer), 0,
                                        reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                ESP_LOGW(visual_tag, "UDP receive error: errno=%d", errno);
                break;
            }
            // Accept only peers on this SoftAP subnet, not Pi5's upstream LAN.
            if ((peer.sin_addr.s_addr & ip.netmask.addr) != (ip.ip.addr & ip.netmask.addr)) continue;
            if (g_hro_shutdown_requested.load(std::memory_order_acquire)) break;
            char nonce[17];
            if (hro::clock::token(buffer, static_cast<std::size_t>(length), nonce)) {
                char response[64];
                const auto current = utc_ms();
                const auto stamp = current < hro::clock::MAX_MS ? current : 0;
                const int n = hro::clock::response(response, sizeof(response), nonce, stamp);
                if (n > 0) sendto(sock, response, n, 0, reinterpret_cast<sockaddr*>(&peer), peer_size);
                continue;
            }
            hro::visual::Request request;
            if (!hro::visual::parse(buffer, static_cast<std::size_t>(length), request)) continue;
            std::uint64_t received_ms = 0;
            const auto uptime = esp_timer_get_time();
            auto result = cache.accept(request, uptime, utc_ms(), received_ms);
            if (result == VisualCache::Result::New && !visual_log_enqueue({request, received_ms, uptime})) {
                cache.forget(request);
                result = VisualCache::Result::Full;
            }
            char response[80];
            int response_length;
            if (result == VisualCache::Result::Full) {
                response_length = hro::visual::busy_reply(response, sizeof(response), request);
            } else {
                if (result == VisualCache::Result::New) {
                    visual_marker_add(received_ms, request.meteor_count);
                    ESP_LOGI(visual_tag, "VISUAL Stick=%s Event=%s UTC-ms=%llu", request.stick_id, request.event_id,
                             static_cast<unsigned long long>(received_ms));
                    console_printf(ConsoleLevel::Info, "VISUAL Stick %s Meteors %lu Count %lu Event %s", request.stick_id, static_cast<unsigned long>(request.meteor_count), static_cast<unsigned long>(request.sequence), request.event_id);
                }
                response_length = hro::visual::ack(response, sizeof(response), request, received_ms);
            }
            if (response_length > 0 && response_length < static_cast<int>(sizeof(response))) {
                if (sendto(sock, response, response_length, 0, reinterpret_cast<sockaddr*>(&peer), peer_size) != response_length)
                    ESP_LOGW(visual_tag, "ACK/NACK send failed: errno=%d", errno);
            }
            taskYIELD();
        }
        close(sock); sock = -1;
        if (!g_hro_shutdown_requested.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    receiver = nullptr;
    vTaskDelete(nullptr);
}
}
bool visual_start() {
    if (receiver) return true;
    if (!visual_log_start()) return false;
    if (xTaskCreate(receive_task, "tab5_visual", 4096, nullptr, 1, &receiver) == pdPASS) return true;
    ESP_LOGE(visual_tag, "VISUAL receiver task creation failed");
    console_message("Stick VISUAL receiver unavailable", ConsoleLevel::Error);
    return false;
}
}
