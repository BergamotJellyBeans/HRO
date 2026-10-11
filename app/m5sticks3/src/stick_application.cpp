#include "stick_application.hpp"
#include "stick_display.hpp"
#include "stick_buttons.hpp"
#include "stick_network.hpp"
#include "stick_time.hpp"
#include "stick_visual.hpp"
#include "stick_audio.hpp"
#include "stick_led.hpp"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_pm.h"
#include "esp_heap_caps.h"

#include <cstdint>
#include <cstdio>
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace stick {
namespace {
std::uint32_t presses = 0;
bool minute_test = false;
const char* reset_name(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external-reset";
        case ESP_RST_SW: return "software-reset";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt-watchdog";
        case ESP_RST_TASK_WDT: return "task-watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "SDIO-reset";
        default: return "other";
    }
}
}
bool test_running() { return minute_test; }
std::uint32_t button_press_count() { return presses; }
void run()
{
    constexpr const char* tag = "sticks3_hro";
    // Keep APB at 80MHz for the existing LGFX peripheral timings. Wi-Fi
    // requests maximum CPU speed when required; idle CPU can run at 80MHz.
    const esp_pm_config_t power_config{160, 80, false};
    const auto power_result = esp_pm_configure(&power_config);
    ESP_LOGI(tag, "CPU power saving 80-160MHz, light sleep off: %s",
        esp_err_to_name(power_result));
    const auto reason = esp_reset_reason();
    ESP_LOGW(tag, "Boot reset reason=%s (%d)", reset_name(reason), static_cast<int>(reason));
    std::uint8_t mac[6] = {};
    const esp_err_t result = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (result != ESP_OK) {
        ESP_LOGE(tag, "Cannot read Wi-Fi STA MAC: %s", esp_err_to_name(result));
        return;
    }
    char stick_id[7] = {};
    std::snprintf(stick_id, sizeof(stick_id), "%02X%02X%02X",
                  static_cast<unsigned>(mac[3]),
                  static_cast<unsigned>(mac[4]),
                  static_cast<unsigned>(mac[5]));
    ESP_LOGI(tag, "M5StickS3-HRO skeleton; Stick ID=%s", stick_id);
    ESP_LOGI(tag, "Clock=unsynchronized; audio=ACK feedback");
    const bool buttons_ready = buttons_begin();
    // Latch B before LCD initialization; holding B at power-on opens settings.
    const bool request_settings = buttons_ready && (buttons_held() & 2);
    const bool display_ready = display_begin(stick_id);
    if (display_ready && !led_begin()) ESP_LOGW(tag, "Notification LED unavailable");
    const bool speaker_ready = display_ready && audio_begin();
    if (!speaker_ready) ESP_LOGW(tag, "ACK speaker unavailable");
    ESP_LOGI(tag, "Startup LCD=%s speaker=%s", display_ready ? "OK" : "failed",
        speaker_ready ? "OK" : "failed");
    network_begin(request_settings);
    display_network(network_view());
    std::uint32_t previous_seconds = 0;
    std::uint8_t pending_buttons = 0;
    bool was_connected = false;
    constexpr std::int64_t test_interval_us = 60000000;
    std::int64_t next_test_event = 0;
    const auto started_at = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        const auto events = buttons_poll();
        bool redraw = network_poll();
        const bool connected = network_view().connected;
        if (connected && !was_connected) audio_connected();
        was_connected = connected;
        if (network_view().selection_mode) pending_buttons |= events;
        else {
            if (events & 2) {
                minute_test = !minute_test && connected;
                next_test_event = esp_timer_get_time() + test_interval_us;
                ESP_LOGI(tag, "60-second VISUAL test %s", minute_test ? "started" : "stopped");
            }
            if (events & 1) {
                if (presses < UINT32_MAX) ++presses;
                display_press_count();
                visual_send(network_view(), stick_id, 1);
            }
        }
        if (minute_test && !connected) {
            minute_test = false;
            ESP_LOGW(tag, "60-second VISUAL test stopped: disconnected");
        }
        const auto now = esp_timer_get_time();
        if (minute_test && now >= next_test_event) {
            visual_send(network_view(), stick_id, 1);
            next_test_event += ((now - next_test_event) / test_interval_us + 1) * test_interval_us;
        }
        // Act on release: a two-button rescan gesture must not save by accident.
        if (network_view().selection_mode && pending_buttons && buttons_held() == 0) {
            if (pending_buttons == 3) network_scan();
            else if (pending_buttons & 1) network_next();
            else if (pending_buttons & 2) network_save();
            pending_buttons = 0;
            redraw = true;
        }
        if (redraw) display_network(network_view());
        clock_poll(network_view());
        visual_poll(network_view());
        const auto seconds = static_cast<std::uint32_t>((esp_timer_get_time() - started_at) / 1000000);
        if (seconds != previous_seconds) {
            previous_seconds = seconds;
            display_runtime();
            if (seconds % 5 == 0) ESP_LOGI(tag, "Alive; Stick ID=%s; mode=%s", stick_id, network_view().selection_mode ? "settings" : "normal");
            if (seconds % 30 == 0) {
                ESP_LOGI(tag, "Health uptime=%lus heap=%u min=%u largest=%u main_stack_free=%u audio_stack_free=%u audio_queue=%u",
                    static_cast<unsigned long>(seconds),
                    static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                    static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                    static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                    audio_stack_free(), audio_queue_size());
            }
        }
    }
}
} // namespace stick
