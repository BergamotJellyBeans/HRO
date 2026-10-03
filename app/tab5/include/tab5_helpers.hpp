#pragma once
#include <cstddef>
#include "tab5_config.h"
#include <cstdint>
#include <ctime>

namespace hro::tab5::app {
struct DmsValue
{
    char direction;
    int degrees;
    int minutes;
    double seconds;
};
DmsValue longitude_to_dms( double value );
DmsValue latitude_to_dms( double value );
void url_decode( char *dst, size_t dst_size, const char *src );
bool get_form_value( const char *body, const char *name, char *out, size_t out_size );
bool point_in_rect( int x, int y, int rx, int ry, int rw, int rh );
bool format_png_filename(const char* prefix, time_t block_start, char* filename, size_t filename_size);
HroTuning calculate_tuning(const Tab5Config& config);
size_t history_physical_index(size_t index, size_t count, size_t write_position, size_t capacity);
}
