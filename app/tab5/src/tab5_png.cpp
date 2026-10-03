#include "tab5_runtime.hpp"
#include "tab5_png.hpp"
#include "tab5_helpers.hpp"

namespace hro::tab5::app {



time_t get_screenshot_block_start( time_t timestamp )
{
    return static_cast<time_t>(hro::plot::blockStart(timestamp));
}

bool make_screenshot_filename( time_t block_start, char *filename,  size_t filename_size )
{ return format_png_filename(g_hro_config.screenshot_prefix, block_start, filename, filename_size); }

void clear_screenshot_info( void )
{
    M5.Display.fillRect( hro::plot::LEFT, 450, 530, CAP_H - 450, TFT_BLACK );
}

void draw_screenshot_info( time_t block_start, const char *filename )
{
    if ( filename == nullptr ) {
        return;
    }

    // UTC Unix time → JST
    const time_t jst_timestamp = block_start + 9 * 60 * 60;

    struct tm jst_tm;
    gmtime_r( &jst_timestamp, &jst_tm );

    char text[128];
    snprintf(
        text,
        sizeof( text ),
        "%04d/%02d/%02d %02d:%02d(JST)    %s",
        jst_tm.tm_year + 1900,
        jst_tm.tm_mon + 1,
        jst_tm.tm_mday,
        jst_tm.tm_hour,
        jst_tm.tm_min,
        filename
    );

    M5.Display.setFont( &fonts::Font2 );
    M5.Display.setTextDatum( middle_left );
    M5.Display.setTextColor( TFT_WHITE, TFT_BLACK );

    // 左側のPeak目盛を避け、グラフ左端の内側に表示する。
    M5.Display.drawString( text, hro::plot::LEFT + 10, 466 );
}

bool save_screenshot_png( time_t block_start, const char *filename )
{
    if ( filename == nullptr || filename[0] == '\0' ) {
        return false;
    }

    // ログ表示用にJSTへ変換
    const time_t jst_timestamp =
        block_start + 9 * 60 * 60;

    struct tm local_tm;
    gmtime_r( &jst_timestamp, &local_tm );

    char path[128];

    snprintf( path, sizeof( path ), "/sdcard/tab5-hro/%s", filename );

    ESP_LOGI(
        TAG,
        "Screenshot: block=%04d/%02d/%02d %02d:%02d:00 JST file=%s",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        filename
    );

    ESP_LOGI( TAG, "Screenshot: start" );

    const int64_t total_start = esp_timer_get_time();

    // ---- LCD -> PNG ----
    size_t png_size = 0;
    const int64_t encode_start = esp_timer_get_time();

    void* png_data = M5.Display.createPng( &png_size, CAP_X, CAP_Y, CAP_W, CAP_H );

    const int64_t encode_end = esp_timer_get_time();
    if ( png_data == nullptr || png_size == 0 ) {
        ESP_LOGE( TAG, "Screenshot: PNG encode failed" );
        if ( png_data != nullptr ) {
            free( png_data );
        }
        return false;
    }

    ESP_LOGI( TAG, "Screenshot: PNG encoded size=%u bytes time=%lld ms", (unsigned)png_size, (long long)(( encode_end - encode_start ) / 1000 ) );

    // ---- PNG -> SD ----
    const int64_t write_start = esp_timer_get_time();

    FILE* fp = fopen( path, "wb" );
    if ( fp == nullptr ) {
        ESP_LOGE( TAG, "Screenshot: fopen failed: %s", path );
        free( png_data );
        return false;
    }

    const size_t written = fwrite( png_data, 1, png_size, fp );
    fclose( fp );

    const int64_t write_end = esp_timer_get_time();

    free( png_data );

    if ( written != png_size ) {
        ESP_LOGE( TAG, "Screenshot: short write %u / %u bytes", (unsigned)written, (unsigned)png_size );
        return false;
    }

    const int64_t total_end = esp_timer_get_time();
    ESP_LOGI( TAG, "Screenshot: saved %s write=%lld ms total=%lld ms", path, (long long)(( write_end - write_start ) / 1000 ), (long long)(( total_end - total_start ) / 1000 ) );
    return true;
}

} // namespace hro::tab5::app
