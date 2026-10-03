#pragma once
#include <cstdint>

namespace hro::tab5::app {
void hro_display_task( void *arg );
const float *get_hro_history( uint16_t history_index );
}
