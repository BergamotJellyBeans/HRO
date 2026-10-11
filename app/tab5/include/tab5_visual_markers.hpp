#pragma once
#include <cstdint>
#include <ctime>
namespace hro::tab5::app {
void visual_marker_add(std::uint64_t received_ms, std::uint32_t meteor_count);
std::uint32_t visual_marker_revision();
void draw_visual_markers(time_t axis_end = 0);
}
