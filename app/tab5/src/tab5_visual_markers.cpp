#include "tab5_visual_markers.hpp"
#include "tab5_visual_marker_history.hpp"
#include "tab5_runtime.hpp"
#include <atomic>
namespace hro::tab5::app {
namespace {
VisualMarkerHistory history;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<std::uint32_t> revision{0};
// Only the display task calls draw; buffers stay off its stack.
std::uint32_t counts[hro::plot::WIDTH];
unsigned previous_height[hro::plot::WIDTH] = {};

}
void visual_marker_add(std::uint64_t received_ms, std::uint32_t meteor_count) {
    if (received_ms < 1577836800000ULL || !meteor_count) return;
    portENTER_CRITICAL(&lock);
    history.add(received_ms, meteor_count);
    portEXIT_CRITICAL(&lock);
    revision.fetch_add(1, std::memory_order_release);
}
std::uint32_t visual_marker_revision() { return revision.load(std::memory_order_acquire); }
void draw_visual_markers(time_t axis_end) {
    if (!axis_end) axis_end = time(nullptr);
    portENTER_CRITICAL(&lock);
    for (int x = 0; x < hro::plot::WIDTH; ++x) counts[x] = history.column(x, axis_end);
    portEXIT_CRITICAL(&lock);
    // Keep the original full-size waterfall rendering untouched.
    // No image transfer at all for columns with neither a current nor old marker.
    for (int x = 0; x < WF_W; ++x) {
        const unsigned height = VisualMarkerHistory::height(counts[x]);
        // Restore only the part exposed when a stack becomes shorter or disappears.
        for (unsigned offset = height; offset < previous_height[x]; ++offset) {
            const int local_y = WF_H - 1 - static_cast<int>(offset);
            M5.Display.drawPixel(WF_X + x, WF_Y + local_y,
                g_waterfall.readPixel(x, local_y));
        }
        if (height) {
            M5.Display.drawFastVLine(WF_X + x, WF_Y + WF_H - height,
                height, TFT_YELLOW);
        }
        previous_height[x] = height;
    }
}
}
