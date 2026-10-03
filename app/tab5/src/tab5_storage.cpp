#include "tab5_runtime.hpp"
#include "tab5_storage.hpp"
#include "tab5_sdcard.h"

namespace hro::tab5::app {


void stop_png_storage()
{
    if (g_hro_storage_stopped.load(std::memory_order_acquire)) return;
    if (!device::sdcard_mounted()) {
        g_hro_storage_stopped.store(true, std::memory_order_release);
        return;
    }
    if (g_audio_config_dirty.load(std::memory_order_relaxed)) {
        stored_hro_config().audio_volume = g_audio_volume.load(std::memory_order_relaxed);
        stored_hro_config().audio_mute = g_audio_mute.load(std::memory_order_relaxed);
        if (!save_hro_config()) {
            ESP_LOGE(TAG, "Pending audio config could not be saved; SD remains mounted");
            return;
        }
        g_audio_config_dirty.store(false, std::memory_order_relaxed);
    }
    const esp_err_t err = device::unmount_sdcard();
    if (err == ESP_OK) {
        g_hro_storage_stopped.store(true, std::memory_order_release);
    } else {
        ESP_LOGE(TAG, "PNG SD unmount failed: %s", esp_err_to_name(err));
    }
}

// The application owns save/shutdown policy; the platform owns device access.
void list_sdcard_root() { device::list_sdcard_root(); }
esp_err_t init_sdcard() { return device::init_sdcard(); }
uint8_t* load_file_to_psram(const char* path, size_t* out_size)
{
    return device::load_file_to_psram(path, out_size);
}

} // namespace hro::tab5::app
