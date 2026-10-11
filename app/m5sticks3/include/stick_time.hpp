#pragma once
#include <cstddef>
namespace stick {
struct NetworkView;
void clock_poll(const NetworkView& network);
void clock_text(char* out, std::size_t size);
bool clock_synced();
}
