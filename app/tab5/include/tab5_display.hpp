#pragma once

#include <ctime>

namespace hro::tab5::app {
void draw_system_info();
void log_battery_status( void );
void draw_battery_status( void );
void draw_waterfall_time_axis( time_t axis_end = 0 );
void draw_waterfall_column( const float *spectrum );
void draw_hro_level_history( void );
void draw_current_time(time_t observation_time = 0);
bool load_base_screen( void );
void draw_waterfall_frequency_axis( void );
void draw_station_info( void );
}
