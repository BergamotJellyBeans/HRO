#pragma once

namespace hro::tab5::app {
enum class ConsoleLevel { Info, Warning, Error };
void console_init(); // Called at startup before producer tasks are started.
void console_message(const char* text, ConsoleLevel level = ConsoleLevel::Info);
void console_printf(ConsoleLevel level, const char* format, ...) __attribute__((format(printf, 2, 3)));
void console_tick(bool force = false); // Only the LCD task draws.
}
