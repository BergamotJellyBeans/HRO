#include "tab5_field.hpp"
#include "tab5_runtime.hpp"
#include "tab5_observation.hpp"
#include "tab5_audio.hpp"
#include "tab5_controls.hpp"
#include "tab5_display.hpp"
#include "tab5_helpers.hpp"
#include "tab5_png.hpp"
#include "tab5_storage.hpp"
#include "tab5_terminal.hpp"
#include "tab5_console.hpp"
#include "tab5_visual_markers.hpp"

namespace hro::tab5::app {
static time_t g_screenshot_block_start = 0;


void hro_display_task( void *arg )
{
    uint32_t last_sequence = 0;
    unsigned draw_count = 0;
    std::uint32_t marker_revision = 0;
    static int64_t last_battery_update_us = 0;
    static bool safe_displayed = false;
    bool screenshot_full_block_ready = false;

    while ( true ) {
        M5.update();
        handle_audio_touch(); // タッチパネル操作
        if (g_hro_shutdown_requested.load(std::memory_order_acquire)) {
            console_tick(true);
            stop_png_storage();
            if (!safe_displayed && g_hro_storage_stopped.load(std::memory_order_acquire)) {
                draw_shutdown_button(true);
                safe_displayed = true;
                ESP_LOGI(TAG, "SD unmounted; powering off");
                console_message("SD card unmounted; powering off");
                console_tick(true);
                vTaskDelay(pdMS_TO_TICKS(300));
                M5.Power.powerOff();
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if ( !safe_displayed && g_hro_storage_stopped.load( std::memory_order_acquire ) ) {
            draw_shutdown_button( true );
            safe_displayed = true;
        }
        if ( !g_hro_shutdown_requested.load( std::memory_order_acquire ) ) {
            save_audio_config_if_needed();	// Audio設定変更から2秒後にSDへ保存
        }

        poll_terminal_display();
        poll_phone_field();
        console_tick();
        const uint32_t sequence = g_hro_spectrum_sequence.load( std::memory_order_acquire );

        if ( sequence != last_sequence ) {
            last_sequence = sequence;

            const time_t now = terminal_mode() ? terminal_observation_time() : time(nullptr);
            // PNG recording belongs only to standalone observation.
            if (!terminal_mode()) {
                const time_t current_block_start = get_screenshot_block_start( now );

                if ( g_screenshot_block_start == 0 ) {
                    // NTP同期後、現在の20分ブロックに参加する。
                    // このブロックは途中からなので保存対象にはしない。
                    g_screenshot_block_start = current_block_start;
                    screenshot_full_block_ready = false;
                    ESP_LOGI( TAG, "Screenshot block started: %lld (partial block)", static_cast<long long>( g_screenshot_block_start ) );

                } else if ( current_block_start != g_screenshot_block_start ) {
                    const time_t completed_block_start = g_screenshot_block_start;
                    if ( screenshot_full_block_ready ) {
                        char filename[64];
                        if ( make_screenshot_filename( completed_block_start, filename, sizeof( filename ) ) ) {
                            ESP_LOGI( TAG, "Screenshot block completed: filename=%s", filename );
                        }
                        // 時刻ラベル正規化
                        draw_waterfall_time_axis( completed_block_start + TOTAL_SEC );

                        // PNGに記録する情報を一時的にLCDへ表示
                        draw_screenshot_info( completed_block_start, filename );

                        // 表示した情報も含めてPNG保存
                        console_printf(ConsoleLevel::Info, "PNG saving: %s", filename);
                        console_tick(true);
                        draw_visual_markers(completed_block_start + TOTAL_SEC - 1);
                        const bool saved = save_screenshot_png( completed_block_start, filename );
                        console_printf(saved ? ConsoleLevel::Info : ConsoleLevel::Error,
                                       saved ? "PNG saved: %s" : "PNG save failed: %s", filename);

                        // 一時表示を消す
                        clear_screenshot_info();

                        if ( !saved ) {
                            ESP_LOGE( TAG, "Screenshot block save failed: %s", filename );
                        }
                    } else {
                        ESP_LOGI( TAG, "Screenshot partial block skipped" );
                    }

                    // 新しい20分ブロックへ切り替える
                    g_screenshot_block_start = current_block_start;
                    screenshot_full_block_ready = true;

                    ESP_LOGI( TAG, "Screenshot block started: %lld", static_cast<long long>( g_screenshot_block_start ) );
                }
            }

            // 境界処理が終わってから今回のSpectrumを描く
            const int64_t draw_start_us = esp_timer_get_time();
            draw_waterfall_column( g_hro_spectrum );
            marker_revision = visual_marker_revision();
            draw_visual_markers(now);
            const int64_t waterfall_done_us = esp_timer_get_time();
            draw_hro_level_history();
            const int64_t level_done_us = esp_timer_get_time();

            const int64_t now_us = esp_timer_get_time();

            if ( now_us - last_battery_update_us >= 10 * 1000 * 1000LL ) {
                last_battery_update_us = now_us;
                log_battery_status();
                draw_battery_status();
            }

            const int64_t axis_start_us = esp_timer_get_time();
            draw_waterfall_time_axis(terminal_mode() ? now : 0);
            const int64_t axis_done_us = esp_timer_get_time();
            draw_current_time(terminal_mode() ? now : 0);
            if (++draw_count % 10 == 0) {
                ESP_LOGI(TAG, "LCD: waterfall=%lld us level=%lld us time_axis=%lld us",
                         static_cast<long long>(waterfall_done_us - draw_start_us),
                         static_cast<long long>(level_done_us - waterfall_done_us),
                         static_cast<long long>(axis_done_us - axis_start_us));
            }
        }
        const auto current_marker_revision = visual_marker_revision();
        if (marker_revision != current_marker_revision) {
            marker_revision = current_marker_revision;
            draw_visual_markers(terminal_mode() ? terminal_observation_time() : time(nullptr));
        }
        vTaskDelay(pdMS_TO_TICKS( 20 ));
    }
}

const float *get_hro_history( uint16_t history_index )
{
    if ( g_hro_history == nullptr ) {
        return nullptr;
    }

    if ( history_index >= g_hro_history_count ) {
        return nullptr;
    }

    const size_t physical_index = history_physical_index(history_index, g_hro_history_count,
                                                         g_hro_history_write_pos, HRO_HISTORY_SECONDS);
    return &g_hro_history[static_cast<size_t>(physical_index) * HRO_BIN_COUNT];
}

} // namespace hro::tab5::app
