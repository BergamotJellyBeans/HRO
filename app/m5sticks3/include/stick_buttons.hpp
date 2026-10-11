#pragma once
#include <cstdint>
namespace stick {
bool buttons_begin();
// Returns bit 0 for A / bit 1 for B, once per debounced press.
std::uint8_t buttons_poll();
std::uint8_t buttons_held();
}
