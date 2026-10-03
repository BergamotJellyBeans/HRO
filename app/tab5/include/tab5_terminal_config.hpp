#pragma once
#include "tab5_config.h"
#include <cstdint>
#include <cstddef>

namespace hro::tab5::app {
bool decode_terminal_config(const char* data, std::size_t size, uint32_t request_id,
                            const Tab5Config& local, Tab5Config& output);
}
