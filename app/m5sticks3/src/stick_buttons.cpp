#include "stick_buttons.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

namespace stick {
namespace {
// Verified against M5Unified 0.2.20 StickS3 init/update: A=11, B=12, active low.
constexpr gpio_num_t pins[] = {GPIO_NUM_11, GPIO_NUM_12};
struct State { bool candidate; bool stable; std::int64_t changed_at; };
State states[2] = {};
bool ready = false;
constexpr std::int64_t debounce_us = 30000;
}
bool buttons_begin()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12);
    cfg.mode = GPIO_MODE_INPUT;
    // Match official M5Unified input configuration; board has external bias.
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    const auto result = gpio_config(&cfg);
    if (result != ESP_OK) {
        ESP_LOGE("stick_buttons", "Input init failed: %s", esp_err_to_name(result));
        return false;
    }
    const auto now = esp_timer_get_time();
    for (unsigned i = 0; i < 2; ++i) {
        const bool pressed = gpio_get_level(pins[i]) == 0;
        states[i] = {pressed, pressed, now};
    }
    ready = true;
    return true;
}
std::uint8_t buttons_held()
{
    if (!ready) return 0;
    return (states[0].stable ? 1 : 0) | (states[1].stable ? 2 : 0);
}
std::uint8_t buttons_poll()
{
    if (!ready) return 0;
    const auto now = esp_timer_get_time();
    std::uint8_t events = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const bool pressed = gpio_get_level(pins[i]) == 0;
        auto& s = states[i];
        if (pressed != s.candidate) {
            s.candidate = pressed;
            s.changed_at = now;
        }
        if (s.candidate != s.stable && now - s.changed_at >= debounce_us) {
            s.stable = s.candidate;
            if (s.stable) events |= 1U << i;
        }
    }
    return events;
}
}
