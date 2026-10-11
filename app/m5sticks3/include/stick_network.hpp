#pragma once
#include <cstddef>
namespace stick {
constexpr std::size_t kMaxTab5Candidates = 8;
struct Tab5Candidate { char ssid[16]; int rssi; bool open; };
struct NetworkView {
    Tab5Candidate candidates[kMaxTab5Candidates] = {};
    unsigned count = 0;
    unsigned selected = 0;
    char saved_ssid[16] = {};
    char status[32] = {};
    char ip_address[16] = {};
    char gateway[16] = {};
    bool connected = false;
    bool selection_mode = false;
    bool scanning = false;
    bool available = false;
};
void network_begin(bool request_settings);
void network_scan();
bool network_poll();
void network_next();
void network_save();
const NetworkView& network_view();
}
