#include "stick_display.hpp"
#include "stick_logo.hpp"
#include "stick_network.hpp"
#include "stick_time.hpp"
#include "stick_visual.hpp"
#include "stick_application.hpp"
#include "stick_audio.hpp"
#include "M5GFX.h"
#include "esp_log.h"
#include "esp_timer.h"

extern "C" {
extern const unsigned char logo_start[] asm("_binary_hro_logo_png_start");
extern const unsigned char logo_end[] asm("_binary_hro_logo_png_end");
}

namespace stick {
namespace {
M5GFX lcd;
bool ready = false;
constexpr const char* tag = "stick_display";

char battery_text[8] = "--%";

void update_battery()
{
    static int64_t next_read_us = 10000000;
    const int64_t now = esp_timer_get_time();
    if (now < next_read_us || !audio_ready() || audio_busy()) return;
    next_read_us = now + 5000000;
    // M5Unified 0.2.20: utility/power/M5PM1_Class.cpp reads VBAT as
    // little-endian millivolts from 0x22; Power_Class::getBatteryLevel
    // uses this voltage estimate. Use the same locked I2C bus as M5GFX/audio.
    const uint8_t reg = 0x22;
    uint8_t bytes[2] = {};
    const auto result = lgfx::i2c::transactionWriteRead(
        1, 0x6e, &reg, 1, bytes, sizeof(bytes), 100000);
    const unsigned mv = (static_cast<unsigned>(bytes[1]) << 8) | bytes[0];
    if (result.has_value() && mv >= 2500 && mv <= 5000) {
        const int percent = std::max(0, std::min(100,
            (static_cast<int>(mv) - 3300) * 100 / 800));
        std::snprintf(battery_text, sizeof(battery_text), "%d%%", percent);
    } else {
        std::snprintf(battery_text, sizeof(battery_text), "--%%");
        ESP_LOGW(tag, "Battery voltage unavailable");
    }
}
}

bool display_begin(const char* stick_id)
{
    // M5GFX 0.2.27 detects StickS3 via M5PM1 and powers/configures its LCD.
    // No guessed GPIO, PMIC or panel commands are maintained in this app.
    if (!lcd.init() || lcd.getBoard() != m5gfx::board_t::board_M5StickS3) {
        ESP_LOGE(tag, "StickS3 LCD initialization/detection failed");
        return false;
    }
    lcd.setRotation(0); // portrait 135 x 240
    lcd.setBrightness(128);
    lcd.fillScreen(0x000000U);
    lcd.setTextColor(0xFFFFFFU, 0x000000U);
    lcd.setTextSize(1);
    lcd.setCursor(10, 8);
    lcd.print("M5StickS3 HRO");
    lcd.setCursor(10, 19);
    lcd.printf("ID:%s", stick_id);
    constexpr float scale = static_cast<float>(kLogoDisplayWidth) / 113.0f;
    if (!lcd.drawPng(logo_start, static_cast<std::uint32_t>(logo_end - logo_start),
                     (lcd.width() - kLogoDisplayWidth) / 2, 28,
                     0, 0, 0, 0, scale, scale)) {
        ESP_LOGW(tag, "Logo decode failed; boot text remains visible");
    }
    lcd.setCursor(10, 104);
    lcd.print("BOOT OK");
    lcd.setCursor(10, 126);
    lcd.printf("ID: %s", stick_id);
    lcd.setCursor(10, 148);
    lcd.print("Wi-Fi: OFF");

    ready = true;
    ESP_LOGI(tag, "StickS3 LCD ready: %dx%d", lcd.width(), lcd.height());
    display_runtime();
    return true;
}

void display_network(const NetworkView& view)
{
    if (!ready) return;
    lcd.fillRect(0, 100, lcd.width(), 126, 0x000000U);
    lcd.setCursor(6, 101);
    lcd.setTextWrap(false);
    lcd.print(view.status);
    if (!view.selection_mode) {
        lcd.setCursor(6, 129);
        lcd.print(view.saved_ssid);
        lcd.setCursor(6, 145);
        if (view.connected) lcd.printf("IP:%s", view.ip_address);
        else lcd.print("Wi-Fi: offline");
        display_press_count();
        return;
    }
    if (view.count && !view.scanning) {
        const auto& ap = view.candidates[view.selected];
        lcd.setCursor(6, 115);
        lcd.printf("%u/%u %ddBm %s", view.selected + 1, view.count, ap.rssi, ap.open ? "OPEN" : "LOCK");
        lcd.setCursor(6, 129);
        lcd.print(ap.ssid);
    }
    lcd.setCursor(6, 145);
    lcd.printf("Saved:%s", view.saved_ssid[0] ? view.saved_ssid + 9 : "none");
    lcd.setCursor(6, 161);
    lcd.print("A:Next B:Save");
    lcd.setCursor(6, 177);
    lcd.print("A+B:Rescan");
}

void display_press_count()
{
    if (!ready || network_view().selection_mode) return;
    lcd.fillRect(0, 160, lcd.width(), 66, 0x000000U);
    char text[16];
    std::snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(button_press_count()));
    lcd.setFont(&fonts::Font8);
    lcd.setTextSize(1);
    // Keep the count within its existing area, above the clock and battery.
    const float scale = std::min(1.0f, std::min(
        60.0f / lcd.fontHeight(),
        static_cast<float>(lcd.width() - 12) / lcd.textWidth(text)));
    lcd.setTextSize(scale);
    lcd.setTextDatum(middle_center);
    lcd.drawString(text, lcd.width() / 2, 193);
    lcd.setTextDatum(top_left);
    lcd.setFont(&fonts::Font0);
    lcd.setTextSize(1);

}

void display_runtime()
{
    if (!ready) return;
    update_battery();
    if (!network_view().selection_mode) {
        lcd.fillRect(0, 113, lcd.width(), 12, 0x000000U);
        lcd.setCursor(6, 113); lcd.print(visual_status());
    }
    display_press_count();
    lcd.fillRect(0, 226, lcd.width(), 14, 0x000000U);
    char text[20]; clock_text(text, sizeof(text));
    lcd.setCursor(6, 228);
    lcd.print(text);
    lcd.setTextDatum(top_right);
    lcd.drawString(battery_text, lcd.width() - 6, 228);
    lcd.setTextDatum(top_left);
}
}
