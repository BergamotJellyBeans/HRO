#include "tab5_console.hpp"
#include "tab5_runtime.hpp"
#include "tab5_controls.hpp"
#include "tab5_audio.hpp"
#include "tab5_helpers.hpp"
#include "tab5_display.hpp"

namespace hro::tab5::app {
static bool g_shutdown_holding = false;
static int64_t g_shutdown_press_us = 0;


void draw_shutdown_button( bool safe )
{
    M5.Display.setTextColor( WHITE );
    M5.Display.setTextDatum( middle_center );

    if ( safe ) {
        M5.Display.fillRoundRect( SHUTDOWN_X, SHUTDOWN_Y, SHUTDOWN_W, SHUTDOWN_H, 6, RED );
        M5.Display.drawRoundRect( SHUTDOWN_X, SHUTDOWN_Y, SHUTDOWN_W, SHUTDOWN_H, 6, WHITE );
        M5.Display.setFont( &fonts::Font2 );
        M5.Display.drawString( "POWERING OFF...", SHUTDOWN_X + SHUTDOWN_W / 2, SHUTDOWN_Y + SHUTDOWN_H / 2 );
    } else {
        M5.Display.fillRoundRect( SHUTDOWN_X, SHUTDOWN_Y, SHUTDOWN_W, SHUTDOWN_H, 6, DARKGREY );
        M5.Display.drawRoundRect( SHUTDOWN_X, SHUTDOWN_Y, SHUTDOWN_W, SHUTDOWN_H, 6, WHITE );
        M5.Display.setFont( &fonts::Font2 );
        M5.Display.drawString( "HOLD 2 SEC TO SHUTDOWN", SHUTDOWN_X + SHUTDOWN_W / 2, SHUTDOWN_Y + SHUTDOWN_H / 2 );
    }
}

void draw_audio_controls( void )
{
    constexpr int PANEL_X = 1015, PANEL_Y = 520, PANEL_W = 250;
    constexpr int BTN_Y = AUDIO_VOL_MINUS_Y, BTN_W = 62, BTN_H = 38;
    constexpr int MINUS_X = AUDIO_VOL_MINUS_X, PLUS_X = AUDIO_VOL_PLUS_X;
    constexpr int MUTE_X = AUDIO_MUTE_X, MUTE_Y = AUDIO_MUTE_Y;
    constexpr int MUTE_W = AUDIO_MUTE_W, MUTE_H = AUDIO_MUTE_H;
    M5.Display.fillRect(PANEL_X, PANEL_Y, PANEL_W, 161, BLACK);
    M5.Display.drawRect(PANEL_X, PANEL_Y, PANEL_W, 161, WHITE);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(WHITE);
    M5.Display.drawString("DISPLAY LEVEL", 1140, 533);
    M5.Display.drawRect(1025, 546, 78, 32, WHITE);
    M5.Display.drawRect(1177, 546, 78, 32, WHITE);
    M5.Display.drawString("LEVEL-", 1064, 562);
    M5.Display.drawString("LEVEL+", 1216, 562);
    char level[16];
    snprintf(level, sizeof(level), "%+d dB", g_display_level_db.load(std::memory_order_relaxed));
    M5.Display.setTextColor(CYAN);
    M5.Display.drawString(level, 1140, 562);

    const int volume = g_audio_volume.load( std::memory_order_relaxed );
    const bool mute = g_audio_mute.load( std::memory_order_relaxed );

    // タイトル
    M5.Display.setFont( &fonts::Font2 );
    M5.Display.setTextDatum( middle_center );
    M5.Display.setTextColor( WHITE );

    M5.Display.drawString( "AUDIO", PANEL_X + PANEL_W / 2, 594 );

    // VOL -
    M5.Display.drawRect( MINUS_X, BTN_Y, BTN_W, BTN_H, WHITE );
    M5.Display.drawString( "VOL-", MINUS_X + BTN_W / 2, BTN_Y + BTN_H / 2 );

    // VOL +
    M5.Display.drawRect( PLUS_X, BTN_Y, BTN_W, BTN_H, WHITE );
    M5.Display.drawString( "VOL+", PLUS_X + BTN_W / 2, BTN_Y + BTN_H / 2 );

    // 音量表示領域を消去
    M5.Display.fillRect( 1090, BTN_Y, 95, BTN_H, BLACK );

    // 現在音量
    char buf[16];
    snprintf( buf, sizeof( buf ), "%d%%", volume );

    M5.Display.setTextColor( CYAN );
    M5.Display.drawString( buf, PANEL_X + PANEL_W / 2, BTN_Y + BTN_H / 2 );

    // MUTEボタン領域を消去してから
    M5.Display.fillRect( MUTE_X, MUTE_Y, MUTE_W, MUTE_H, BLACK );
    M5.Display.drawRect( MUTE_X, MUTE_Y, MUTE_W, MUTE_H, mute ? RED : WHITE );
    M5.Display.setTextColor( mute ? RED : WHITE );
    M5.Display.drawString( mute ? "MUTED" : "MUTE", MUTE_X + MUTE_W / 2, MUTE_Y + MUTE_H / 2 );
}

void handle_audio_touch()
{
    auto detail = M5.Touch.getDetail();

    const int x = detail.x;
    const int y = detail.y;

    // ---------------------------------------------------------
    // SHUTDOWN : 2秒長押し
    // ---------------------------------------------------------

    if ( detail.wasPressed() &&
         point_in_rect(
             x, y,
             SHUTDOWN_X,
             SHUTDOWN_Y,
             SHUTDOWN_W,
             SHUTDOWN_H ) ) {

        g_shutdown_holding = true;
        g_shutdown_press_us = esp_timer_get_time();
        // 長押しの開始を短いタッチ音で知らせる。
        play_touch_beep();

        ESP_LOGI( TAG, "SHUTDOWN hold started" );
        return;
    }

    if ( g_shutdown_holding ) {
        // 指を離した、またはボタン領域から外れた
        if ( !detail.isPressed() ||
             !point_in_rect(
                 x, y,
                 SHUTDOWN_X,
                 SHUTDOWN_Y,
                 SHUTDOWN_W,
                 SHUTDOWN_H ) ) {

            g_shutdown_holding = false;

            ESP_LOGI( TAG, "SHUTDOWN hold cancelled" );
            return;
        }

        // 2秒長押し成立
        if ( esp_timer_get_time() - g_shutdown_press_us >= SHUTDOWN_HOLD_US ) {
            g_shutdown_holding = false;
            // 2秒長押し成立を音で知らせる
            play_touch_beep();

            ESP_LOGI( TAG, "SHUTDOWN requested" );
            console_message("Shutdown started");
            g_hro_shutdown_requested.store( true, std::memory_order_release );
            return;
        }

        return;
    }

    // ---------------------------------------------------------
    // AUDIO : 従来通り、押した瞬間だけ処理
    // ---------------------------------------------------------
    if ( !detail.wasPressed() ) {
        return;
    }

    if (point_in_rect(x, y, 1025, 546, 78, 32) ||
        point_in_rect(x, y, 1177, 546, 78, 32)) {
        const int old = g_display_level_db.load(std::memory_order_relaxed);
        const int level = std::clamp(old + (x < 1140 ? -1 : 1), -30, 30);
        if (level != old) {
            g_display_level_db.store(level, std::memory_order_relaxed);
            audio_config_changed();
        }
        play_touch_beep();
        draw_audio_controls();
        return;
    }

    if ( point_in_rect(
            x, y,
            AUDIO_VOL_MINUS_X,
            AUDIO_VOL_MINUS_Y,
            AUDIO_VOL_MINUS_W,
            AUDIO_VOL_MINUS_H ) ) {

        int volume = g_audio_volume.load( std::memory_order_relaxed );
        volume -= 10;
        if ( volume < 0 ) {
            volume = 0;
        }

        g_audio_volume.store( volume, std::memory_order_relaxed );
        play_touch_beep();
        audio_config_changed();
        draw_audio_controls();
        return;
    }

    if ( point_in_rect(
            x, y,
            AUDIO_VOL_PLUS_X,
            AUDIO_VOL_PLUS_Y,
            AUDIO_VOL_PLUS_W,
            AUDIO_VOL_PLUS_H ) ) {

        int volume = g_audio_volume.load(std::memory_order_relaxed );

        volume += 10;
        if ( volume > 100 ) {
            volume = 100;
        }

        g_audio_volume.store( volume, std::memory_order_relaxed );
        play_touch_beep();
        audio_config_changed();
        draw_audio_controls();
        return;
    }

    if ( point_in_rect(
            x, y,
            AUDIO_MUTE_X,
            AUDIO_MUTE_Y,
            AUDIO_MUTE_W,
            AUDIO_MUTE_H ) ) {

        const bool mute = g_audio_mute.load(std::memory_order_relaxed );
        g_audio_mute.store( !mute, std::memory_order_relaxed );
        play_touch_beep();
        audio_config_changed();
        draw_audio_controls();
    }
}

} // namespace hro::tab5::app
