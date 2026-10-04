#include "tab5_runtime.hpp"
#include "tab5_console.hpp"
#include <cstdarg>

namespace hro::tab5::app {
namespace {
constexpr int X = 10, Y = 480, W = 1000, H = 195;
constexpr int PADDING = 4, LINE_HEIGHT = 20;
constexpr int ROWS = (H - 2 * PADDING) / LINE_HEIGHT;
struct Message {
    char text[104];
    ConsoleLevel level;
    time_t timestamp;
    uint32_t uptime;
};
struct Row { Message message; unsigned repeats; };
M5Canvas canvas(&M5.Display);
QueueHandle_t messages = nullptr;
std::atomic<unsigned> dropped{0};
Row lines[ROWS]{};
int count = 0;
bool ready = false;
bool dirty = true;
int64_t last_draw_us = 0;

void append(const Message& message)
{
    if (count && lines[count - 1].message.level == message.level &&
        strcmp(lines[count - 1].message.text, message.text) == 0) {
        ++lines[count - 1].repeats;
    } else {
        if (count == ROWS) {
            memmove(lines, lines + 1, sizeof(lines[0]) * (ROWS - 1));
            --count;
        }
        lines[count++] = {message, 1};
    }
    dirty = true;
}
}

void console_init()
{
    messages = xQueueCreate(32, sizeof(Message));
    if (!messages) ESP_LOGE(TAG, "Failed to create console message queue");
}

void console_message(const char* text, ConsoleLevel level)
{
    if (!messages || !text) return;
    Message message{};
    snprintf(message.text, sizeof(message.text), "%s", text);
    message.level = level;
    message.timestamp = time(nullptr);
    message.uptime = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    if (xQueueSend(messages, &message, 0) != pdTRUE) dropped.fetch_add(1);
}

void console_printf(ConsoleLevel level, const char* format, ...)
{
    char text[104];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    console_message(text, level);
}

void console_tick(bool force)
{
    if (!messages) return;
    Message message{};
    while (xQueueReceive(messages, &message, 0) == pdTRUE) append(message);
    const unsigned lost = dropped.exchange(0);
    if (lost) {
        snprintf(message.text, sizeof(message.text), "Console queue full: %u messages skipped", lost);
        message.level = ConsoleLevel::Warning;
        message.timestamp = time(nullptr);
        message.uptime = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
        append(message);
    }
    const int64_t now = esp_timer_get_time();
    if (!dirty || (!force && now - last_draw_us < 100000)) return;
    if (!ready) {
        canvas.setPsram(true);
        canvas.setColorDepth(8);
        if (!canvas.createSprite(W, H)) return;
        canvas.setFont(&fonts::Font2);
        canvas.setTextSize(1);
        canvas.setTextDatum(top_left);
        canvas.setTextWrap(false);
        ready = true;
    }
    canvas.fillSprite(BLACK);
    canvas.drawRect(0, 0, W, H, DARKGREY);
    for (int row = 0; row < count; ++row) {
        const auto& entry = lines[row];
        char stamp[24], text[160];
        if (entry.message.timestamp >= 1577836800) {
            // Explicit JST offset avoids changing the shared process timezone.
            const time_t jst = entry.message.timestamp + 9 * 3600;
            tm local{};
            gmtime_r(&jst, &local);
            strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        } else {
            snprintf(stamp, sizeof(stamp), "+%lus", static_cast<unsigned long>(entry.message.uptime));
        }
        if (entry.repeats > 1)
            snprintf(text, sizeof(text), "%s [Tab5] %s (x%u)", stamp, entry.message.text, entry.repeats);
        else snprintf(text, sizeof(text), "%s [Tab5] %s", stamp, entry.message.text);
        const uint16_t color = entry.message.level == ConsoleLevel::Error ? RED :
                               entry.message.level == ConsoleLevel::Warning ? YELLOW : CYAN;
        canvas.setTextColor(color, BLACK);
        canvas.drawString(text, PADDING, PADDING + row * LINE_HEIGHT);
    }
    canvas.pushSprite(X, Y);
    dirty = false;
    last_draw_us = now;
}
}
