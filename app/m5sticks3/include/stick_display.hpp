#pragma once
#include <cstdint>
namespace stick {
struct NetworkView;
void display_network(const NetworkView& view);
void display_press_count();
bool display_begin(const char* stick_id);
void display_runtime();
}
