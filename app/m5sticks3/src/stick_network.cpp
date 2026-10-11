#include "stick_network.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <new>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace stick {
namespace {
NetworkView view;
std::atomic<bool> scan_done{false};
std::atomic<std::uint32_t> scan_result{0};
constexpr const char* tag = "stick_network";
enum class LinkEvent { Associated, Address, Disconnected };
struct LinkUpdate {
    LinkEvent event;
    esp_ip4_addr_t address;
    esp_ip4_addr_t gateway;
    unsigned reason;
};
QueueHandle_t link_updates = nullptr;
bool connecting = false;
bool connection_configured = false;
std::int64_t retry_at_us = 0;
std::int64_t attempt_deadline_us = 0;
constexpr std::int64_t RETRY_US = 3000000;
constexpr std::int64_t CONNECT_TIMEOUT_US = 15000000;
void status(const char* text) { std::snprintf(view.status, sizeof(view.status), "%s", text); }
bool check(esp_err_t err, const char* operation) {
    if (err == ESP_OK) return true;
    ESP_LOGE(tag, "%s: %s", operation, esp_err_to_name(err));
    status(operation);
    return false;
}
bool matches(const char* ssid) {
    if (std::strlen(ssid) != 15 || std::strncmp(ssid, "Tab5-HRO-", 9) != 0) return false;
    for (unsigned i = 9; i < 15; ++i) {
        const char c = ssid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
void on_scan(void*, esp_event_base_t, std::int32_t, void* data) {
    scan_result.store(static_cast<wifi_event_sta_scan_done_t*>(data)->status);
    scan_done.store(true);
}
// Event loop publishes a latest-state snapshot; only app_main mutates the UI.
void on_link(void*, esp_event_base_t base, std::int32_t id, void* data) {
    LinkUpdate update{};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        update.event = LinkEvent::Associated;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        update.event = LinkEvent::Disconnected;
        update.reason = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        update.event = LinkEvent::Address;
        const auto* event = static_cast<ip_event_got_ip_t*>(data);
        update.address = event->ip_info.ip;
        update.gateway = event->ip_info.gw;
    } else return;
    xQueueOverwrite(link_updates, &update);
}
void try_connect() {
    status("Connecting...");
    view.connected = false;
    const auto err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(tag, "Connect attempt: %s", esp_err_to_name(err));
        connecting = false;
        retry_at_us = esp_timer_get_time() + RETRY_US;
        status("Retry in 3s");
        return;
    }
    connecting = true;
    retry_at_us = 0;
    attempt_deadline_us = esp_timer_get_time() + CONNECT_TIMEOUT_US;
}
void start_connection() {
    if (!view.available || view.selection_mode || !view.saved_ssid[0]) return;
    wifi_config_t config = {};
    std::memcpy(config.sta.ssid, view.saved_ssid, std::strlen(view.saved_ssid));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    // Tab5's existing SoftAP is open. Credentials and implicit NVS writes are absent.
    if (!check(esp_wifi_set_config(WIFI_IF_STA, &config), "Connect config error")) return;
    connection_configured = true;
    view.ip_address[0] = view.gateway[0] = '\0';
    ESP_LOGI(tag, "Connecting to saved Tab5 %s", view.saved_ssid);
    try_connect();
}
bool poll_connection() {
    if (view.selection_mode || !view.available || !connection_configured) return false;
    bool changed = false;
    const auto now = esp_timer_get_time();
    LinkUpdate update{};
    if (xQueueReceive(link_updates, &update, 0) == pdTRUE) {
        changed = true;
        if (update.event == LinkEvent::Address) {
            connecting = false;
            retry_at_us = 0;
            view.connected = true;
            std::snprintf(view.ip_address, sizeof(view.ip_address), IPSTR, IP2STR(&update.address));
            std::snprintf(view.gateway, sizeof(view.gateway), IPSTR, IP2STR(&update.gateway));
            status("Connected");
            ESP_LOGI(tag, "Tab5 connected: SSID=%s IP=%s gateway=%s", view.saved_ssid, view.ip_address, view.gateway);
        } else if (update.event == LinkEvent::Associated) {
            status("Getting IP...");
        } else {
            connecting = false;
            view.connected = false;
            view.ip_address[0] = view.gateway[0] = '\0';
            retry_at_us = now + RETRY_US;
            status("Disconnected; retry");
            ESP_LOGW(tag, "Tab5 disconnected: reason=%u", update.reason);
        }
    }
    if (connecting && now >= attempt_deadline_us) {
        esp_wifi_disconnect();
        connecting = false;
        view.connected = false;
        view.ip_address[0] = view.gateway[0] = '\0';
        retry_at_us = now + RETRY_US;
        status("Timeout; retry");
        changed = true;
    }
    if (!connecting && !view.connected && retry_at_us && now >= retry_at_us) {
        try_connect();
        changed = true;
    }
    return changed;
}
void load_saved() {
    nvs_handle_t handle;
    const auto opened = nvs_open("stick_hro", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return;
    if (!check(opened, "Settings read error")) return;
    std::size_t size = sizeof(view.saved_ssid);
    const auto err = nvs_get_str(handle, "tab5_ssid", view.saved_ssid, &size);
    nvs_close(handle);
    if (err != ESP_OK || !matches(view.saved_ssid)) view.saved_ssid[0] = '\0';
}
}
const NetworkView& network_view() { return view; }
void network_begin(bool request_settings) {
    // Preserve existing NVS: never erase on NO_FREE_PAGES / version mismatch.
    if (!check(nvs_flash_init(), "NVS init error")) return;
    load_saved();
    view.selection_mode = request_settings || !view.saved_ssid[0];
    if (!check(esp_netif_init(), "Network init error")) return;
    if (!check(esp_event_loop_create_default(), "Event init error")) return;
    if (!esp_netif_create_default_wifi_sta()) { status("STA init error"); return; }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = 0; // Only our explicit selection writes NVS.
    if (!check(esp_wifi_init(&config), "Wi-Fi init error")) return;
    if (!check(esp_wifi_set_storage(WIFI_STORAGE_RAM), "Wi-Fi storage error")) return;
    if (!check(esp_wifi_set_mode(WIFI_MODE_STA), "Wi-Fi mode error")) return;
    link_updates = xQueueCreate(1, sizeof(LinkUpdate));
    if (!link_updates) { status("Link queue error"); return; }
    if (!check(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_link, nullptr), "Wi-Fi handler error")) return;
    if (!check(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_link, nullptr), "IP handler error")) return;
    if (!check(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, on_scan, nullptr), "Scan handler error")) return;
    if (!check(esp_wifi_start(), "Wi-Fi start error")) return;
    view.available = true;
    if (view.selection_mode) network_scan();
    else start_connection();
}
void network_scan() {
    if (!view.selection_mode || !view.available || view.scanning) return;
    scan_done.store(false);
    wifi_scan_config_t config = {};
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    if (!check(esp_wifi_scan_start(&config, false), "Scan start error")) return;
    view.scanning = true;
    status("Scanning...");
}
bool network_poll() {
    const bool changed = poll_connection();
    if (!scan_done.exchange(false)) return changed;
    view.scanning = false;
    if (scan_result.load() != 0) {
        esp_wifi_clear_ap_list(); status("Scan failed; retry"); return true;
    }
    view.count = 0;
    view.selected = 0;
    std::uint16_t count = 0;
    if (!check(esp_wifi_scan_get_ap_num(&count), "Scan result error")) {
        esp_wifi_clear_ap_list(); return true;
    }
    if (!count) { esp_wifi_clear_ap_list(); status("No Tab5 found"); return true; }
    std::unique_ptr<wifi_ap_record_t[]> records(new (std::nothrow) wifi_ap_record_t[count]);
    if (!records) { esp_wifi_clear_ap_list(); status("Scan memory error"); return true; }
    if (!check(esp_wifi_scan_get_ap_records(&count, records.get()), "Scan records error")) {
        esp_wifi_clear_ap_list(); return true;
    }
    // IDF returns records by RSSI; keep the strongest eight distinct Tab5 SSIDs.
    for (unsigned i = 0; i < count && view.count < kMaxTab5Candidates; ++i) {
        char ssid[33] = {};
        std::memcpy(ssid, records[i].ssid, 32);
        if (!matches(ssid)) continue;
        bool duplicate = false;
        for (unsigned j = 0; j < view.count; ++j) {
            if (std::strcmp(ssid, view.candidates[j].ssid) == 0) duplicate = true;
        }
        if (duplicate) continue;
        auto& candidate = view.candidates[view.count++];
        std::memcpy(candidate.ssid, ssid, 15);
        candidate.ssid[15] = '\0';
        candidate.rssi = records[i].rssi;
        candidate.open = records[i].authmode == WIFI_AUTH_OPEN;
    }
    for (unsigned i = 0; i < view.count; ++i) {
        if (std::strcmp(view.candidates[i].ssid, view.saved_ssid) == 0) view.selected = i;
    }
    status(view.count ? "Choose Tab5" : "No Tab5 found");
    return true;
}
void network_next() {
    if (!view.selection_mode || view.scanning || !view.count) return;
    view.selected = (view.selected + 1) % view.count;
    status("Choose Tab5");
}
void network_save() {
    if (!view.selection_mode || view.scanning || !view.count) return;
    const auto& candidate = view.candidates[view.selected];
    if (!candidate.open) { status("Locked AP unsupported"); return; }
    nvs_handle_t handle;
    if (!check(nvs_open("stick_hro", NVS_READWRITE, &handle), "Settings open error")) return;
    auto err = nvs_set_str(handle, "tab5_ssid", candidate.ssid);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (!check(err, "Settings save error")) return;
    std::snprintf(view.saved_ssid, sizeof(view.saved_ssid), "%s", candidate.ssid);
    view.selection_mode = false;
    status("Saved; offline");
    ESP_LOGI(tag, "Saved Tab5 SSID=%s", view.saved_ssid);
    start_connection();
}
}
