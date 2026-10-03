#include "tab5_runtime.hpp"
#include "tab5_terminal.hpp"
#include "tab5_terminal_config.hpp"
#include "tab5_wifi.hpp"
#include "tab5_display.hpp"
#include "tab5_helpers.hpp"
#include "hro_live_packet.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <algorithm>
#include <cmath>

namespace hro::tab5::app {
static_assert(HRO_BIN_COUNT == hro::live::BIN_COUNT);
static_assert(AUDIO_BLOCK_SAMPLES == hro::live::AUDIO_SAMPLES);
namespace {
bool g_terminal = false; // Fixed before starting display/audio tasks.
QueueHandle_t g_remote_frames = nullptr;
QueueHandle_t g_remote_config = nullptr;
std::atomic<int> g_status{0};
time_t g_remote_time = 0;
float g_remote_peaks[HRO_HISTORY_SECONDS]{};

void append_peak(float peak)
{
    g_remote_peaks[g_hro_history_write_pos] = peak;
    g_hro_history_write_pos = (g_hro_history_write_pos + 1) % HRO_HISTORY_SECONDS;
    if (g_hro_history_count < HRO_HISTORY_SECONDS) ++g_hro_history_count;
}

int bind_receiver(uint16_t port)
{
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

void terminal_receive_task(void*)
{
    const Tab5Config local = stored_hro_config();
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(50003);
    if (!local.pi5_address[0] || inet_pton(AF_INET, local.pi5_address, &server.sin_addr) != 1) {
        g_status.store(1);
        ESP_LOGW(TAG, "Pi5 IP address is not configured; set it at /station and restart");
        vTaskDelete(nullptr);
        return;
    }
    const int control = bind_receiver(0);
    const int data = bind_receiver(50000);
    const int audio = bind_receiver(50002);
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(4097, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (control < 0 || data < 0 || audio < 0 || !buffer) {
        ESP_LOGE(TAG, "Pi5 UDP receiver initialization failed");
        if (control >= 0) close(control);
        if (data >= 0) close(data);
        if (audio >= 0) close(audio);
        free(buffer);
        g_status.store(5);
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "Pi5 terminal: server=%s control=50003 data=50000 audio=50002", local.pi5_address);
    uint32_t request_id = 0;
    int64_t next_request = 0;
    int64_t lease_until = 0;
    int64_t last_frame_ms = 0;
    int64_t last_data_us = 0;
    bool was_connected = false;
    uint32_t fft_packets = 0, audio_packets = 0, audio_drops = 0;
    int64_t last_log_us = 0;
    hro::live::Frame frame{};
    for (;;) {
        const int64_t now = esp_timer_get_time();
        if (g_hro_shutdown_requested.load(std::memory_order_acquire)) break;
        if (!wifi_sta_ready()) {
            lease_until = 0;
            next_request = 0;
            g_status.store(2);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (now >= next_request) {
            char request[192];
            ++request_id;
            const int len = snprintf(request, sizeof(request),
                "{\"type\":\"register\",\"version\":1,\"request_id\":%lu,"
                "\"device\":\"Tab5-HRO\",\"data_port\":50000,\"audio_port\":50002}",
                static_cast<unsigned long>(request_id));
            sendto(control, request, len, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&server), sizeof(server));
            next_request = now + 5000000;
            if (lease_until <= now) ESP_LOGI(TAG, "Pi5 registration request %lu", static_cast<unsigned long>(request_id));
        }
        const bool connected = lease_until > now;
        if (!connected && was_connected) {
            ESP_LOGW(TAG, "Pi5 registration expired; retrying");
            last_frame_ms = 0;
        }
        was_connected = connected;
        g_status.store(connected ? (last_data_us && now - last_data_us < 3000000 ? 4 : 6) : 3);
        if (now - last_log_us >= 10000000) {
            ESP_LOGI(TAG, "Pi5 RX (10s): FFT=%lu AUDIO=%lu audio_drops=%lu registered=%d",
                     static_cast<unsigned long>(fft_packets), static_cast<unsigned long>(audio_packets),
                     static_cast<unsigned long>(audio_drops), connected ? 1 : 0);
            fft_packets = audio_packets = audio_drops = 0;
            last_log_us = now;
        }
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(control, &reads); FD_SET(data, &reads); FD_SET(audio, &reads);
        timeval wait{0, 20000};
        if (select(std::max(control, std::max(data, audio)) + 1, &reads, nullptr, nullptr, &wait) <= 0)
            continue;
        for (const int sock : {control, data, audio}) {
            if (!FD_ISSET(sock, &reads)) continue;
            sockaddr_in peer{};
            socklen_t peer_size = sizeof(peer);
            const int received = recvfrom(sock, buffer, 4097, 0, reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (received <= 0 || peer.sin_addr.s_addr != server.sin_addr.s_addr) continue;
            if (sock == control) {
                if (peer.sin_port != server.sin_port || received > 4096) continue;
                Tab5Config config{};
                if (!decode_terminal_config(reinterpret_cast<char*>(buffer), received, request_id, local, config)) {
                    ESP_LOGW(TAG, "Pi5 registration response rejected");
                    continue;
                }
                xQueueOverwrite(g_remote_config, &config);
                lease_until = esp_timer_get_time() + 15000000;
                if (!connected) ESP_LOGI(TAG, "Pi5 registered; configuration received");
            } else if (lease_until > esp_timer_get_time() && sock == data) {
                if (!hro::live::decodeFrame(buffer, received, frame) || frame.timestamp_ms <= last_frame_ms)
                    continue;
                last_frame_ms = frame.timestamp_ms;
                last_data_us = esp_timer_get_time();
                ++fft_packets;
                if (xQueueSend(g_remote_frames, &frame, 0) != pdTRUE)
                    ESP_LOGW(TAG, "Pi5 display queue full; dropping FFT frame");
            } else if (lease_until > esp_timer_get_time() && sock == audio) {
                AudioBlock block{};
                if (hro::live::decodeAudio(buffer, received, block.samples)) {
                    ++audio_packets;
                    if (xQueueSend(g_audio_queue, &block, 0) != pdTRUE) ++audio_drops;
                }
            }
        }
    }
    close(control); close(data); close(audio); free(buffer);
    vTaskDelete(nullptr);
}
}

bool terminal_mode() { return g_terminal; }
bool start_terminal_mode()
{
    g_terminal = true;
    preserve_standalone_config();
    g_remote_frames = xQueueCreate(4, sizeof(hro::live::Frame));
    g_remote_config = xQueueCreate(1, sizeof(Tab5Config));
    if (!g_remote_frames || !g_remote_config) return false;
    return xTaskCreate(terminal_receive_task, "pi5_udp", 12288, nullptr, 4, nullptr) == pdPASS;
}

time_t terminal_observation_time() { return g_remote_time; }
float terminal_history_peak(unsigned index)
{
    const auto physical = history_physical_index(index, g_hro_history_count,
                                                g_hro_history_write_pos, HRO_HISTORY_SECONDS);
    return g_remote_peaks[physical];
}

void poll_terminal_display()
{
    if (!g_terminal) return;
    static int previous_status = -1;
    const int status = g_status.load();
    if (status != previous_status) {
        if (previous_status == -1) draw_system_info();
        previous_status = status;
        const char* text = status == 1 ? "Pi5: set IP address at /station and restart" :
                           status == 2 ? "Pi5: waiting for Wi-Fi" :
                           status == 4 ? "Pi5: receiving observation data" :
                           status == 5 ? "Pi5: UDP initialization failed" :
                           status == 6 ? "Pi5: registered, waiting for observation data" :
                                         "Pi5: connecting...";
        M5.Display.fillRect(10, 653, 950, 24, BLACK);
        M5.Display.setFont(&fonts::Font2);
        M5.Display.setTextDatum(top_left);
        M5.Display.setTextColor(CYAN);
        M5.Display.drawString(text, 10, 653);
    }
    Tab5Config config{};
    if (xQueueReceive(g_remote_config, &config, 0) == pdTRUE &&
        memcmp(&config, &g_hro_config, sizeof(config)) != 0) {
        const bool tuning_changed = config.fft_center_hz != g_hro_config.fft_center_hz ||
            config.frequency_hz != g_hro_config.frequency_hz ||
            config.sdr_gain != g_hro_config.sdr_gain ||
            config.level_average_range_hz != g_hro_config.level_average_range_hz;
        g_hro_config = config; // Only the display task applies remote config.
        if (tuning_changed) {
            g_hro_history_count = g_hro_history_write_pos = 0;
            g_waterfall.fillSprite(BLACK);
            g_waterfall.pushSprite(WF_X, WF_Y);
            g_level_graph.fillSprite(BLACK);
            g_level_graph.pushSprite(LEVEL_X, LEVEL_Y);
        }
        draw_station_info();
        draw_system_info();
        draw_waterfall_frequency_axis();
    }
    static hro::live::Frame frame;
    if (xQueueReceive(g_remote_frames, &frame, 0) != pdTRUE) return;
    const time_t timestamp = static_cast<time_t>(frame.timestamp_ms / 1000);
    if (g_remote_time && timestamp > g_remote_time + 1) {
        const unsigned missing = static_cast<unsigned>(std::min<time_t>(timestamp - g_remote_time - 1, HRO_HISTORY_SECONDS));
        if (missing >= WF_W) g_waterfall.fillSprite(BLACK);
        else {
            g_waterfall.scroll(-static_cast<int>(missing), 0);
            g_waterfall.fillRect(WF_W - missing, 0, missing, WF_H, BLACK);
        }
        for (unsigned i = 0; i < missing; ++i) append_peak(NAN);
    }
    g_remote_time = timestamp;
    memcpy(g_hro_spectrum, frame.fft_db, sizeof(g_hro_spectrum));
    append_peak(frame.peak_db);
    g_hro_spectrum_sequence.fetch_add(1, std::memory_order_release);
}
}
