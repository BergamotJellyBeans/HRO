#pragma once
#include <cstddef>
#include <ctime>

namespace hro::tab5::app {
time_t get_screenshot_block_start( time_t timestamp );
bool make_screenshot_filename( time_t block_start, char *filename,  size_t filename_size );
void clear_screenshot_info( void );
void draw_screenshot_info( time_t block_start, const char *filename );
bool save_screenshot_png( time_t block_start, const char *filename );
}
