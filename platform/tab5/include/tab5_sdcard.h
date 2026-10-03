#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace hro::tab5::device {
inline constexpr const char* SD_MOUNT_POINT = "/sdcard";
esp_err_t init_sdcard();
bool sdcard_mounted();
esp_err_t unmount_sdcard();
void list_sdcard_root();
// Returned PSRAM buffer belongs to the caller and must be released with free().
uint8_t* load_file_to_psram(const char* path, size_t* out_size);
}
