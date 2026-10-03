#include "tab5_sdcard.h"
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

namespace hro::tab5::device {
static const char* TAG = "Tab5-HRO";
static sdmmc_card_t* g_sd_card = nullptr;
static sd_pwr_ctrl_handle_t g_sd_pwr = nullptr;

bool sdcard_mounted() { return g_sd_card != nullptr; }
esp_err_t unmount_sdcard()
{
    if (!g_sd_card) return ESP_OK;
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_sd_card);
    if (err == ESP_OK) g_sd_card = nullptr;
    return err;
}

void list_sdcard_root( void )
{
    DIR *dir = opendir( "/sdcard" );

    if ( dir == nullptr ) {
        ESP_LOGE( TAG, "Cannot open /sdcard directory" );
        return;
    }

    ESP_LOGI( TAG, "Files in /sdcard:" );

    struct dirent *entry;

    while ( ( entry = readdir( dir ) ) != nullptr ) {
        ESP_LOGI( TAG, "  %s", entry->d_name );
    }

    closedir( dir );
}

esp_err_t init_sdcard( void )
{
    ESP_LOGI( TAG, "Initializing microSD" );

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };

    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo( &ldo_config, &g_sd_pwr );

    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "SD LDO init failed: %s", esp_err_to_name( err ) );
        return err;
    }

    host.pwr_ctrl_handle = g_sd_pwr;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 4;

    slot_config.clk = GPIO_NUM_43;
    slot_config.cmd = GPIO_NUM_44;
    slot_config.d0  = GPIO_NUM_39;
    slot_config.d1  = GPIO_NUM_40;
    slot_config.d2  = GPIO_NUM_41;
    slot_config.d3  = GPIO_NUM_42;

    // 追加フィールドも含めてゼロ初期化し、利用する設定だけ指定する。
    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 16 * 1024;

    err = esp_vfs_fat_sdmmc_mount( SD_MOUNT_POINT, &host, &slot_config, &mount_config, &g_sd_card );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "microSD mount failed: %s", esp_err_to_name( err ) );
        return err;
    }
    sdmmc_card_print_info( stdout, g_sd_card );
    ESP_LOGI( TAG, "microSD mounted" );

    return ESP_OK;
}

uint8_t *load_file_to_psram( const char *path, size_t *out_size )
{
    FILE *fp = fopen( path, "rb" );
    if ( fp == nullptr ) {
        ESP_LOGE( TAG, "Cannot open %s", path );
        return nullptr;
    }

    fseek( fp, 0, SEEK_END );
    long file_size = ftell( fp );
    rewind( fp );

    if ( file_size <= 0 ) {
        fclose( fp );
        return nullptr;
    }

    uint8_t *buffer = static_cast<uint8_t *>( heap_caps_malloc( file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );
    if ( buffer == nullptr ) {
        ESP_LOGE( TAG, "PSRAM allocation failed: %ld bytes", file_size );
        fclose( fp );
        return nullptr;
    }

    size_t read_size = fread( buffer, 1, file_size, fp );
    fclose( fp );

    if ( read_size != static_cast<size_t>( file_size ) ) {
        ESP_LOGE( TAG, "PNG read failed" );
        free( buffer );
        return nullptr;
    }
    *out_size = read_size;
    ESP_LOGI( TAG, "Loaded PNG: %u bytes", static_cast<unsigned>( read_size ) );
    return buffer;
}


} // namespace hro::tab5::device
