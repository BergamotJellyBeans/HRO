#pragma once
#include <ctime>

namespace hro::tab5::app {
bool terminal_mode();
bool start_terminal_mode();
void poll_terminal_display(); // Called only by the LCD/display task.
time_t terminal_observation_time();
float terminal_history_peak(unsigned index);
}
