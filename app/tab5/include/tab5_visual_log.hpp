#pragma once
#include "tab5_visual_log_record.hpp"
namespace hro::tab5::app {
bool visual_log_start();
bool visual_log_enqueue(const VisualLogRecord& record);
// Gates new entries, then waits briefly for the writer to drain and close files.
bool visual_log_stop();
}
