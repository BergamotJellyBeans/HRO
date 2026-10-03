#pragma once
#include <cstdint>
#include <cstddef>
#include "esp_err.h"

namespace hro::tab5::app {
void stop_png_storage();
void list_sdcard_root( void );
esp_err_t init_sdcard( void );
uint8_t *load_file_to_psram( const char *path, size_t *out_size );
}
