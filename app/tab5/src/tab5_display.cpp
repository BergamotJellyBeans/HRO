#include "tab5_runtime.hpp"
#include "tab5_display.hpp"
#include "tab5_helpers.hpp"
#include "tab5_observation.hpp"
#include "tab5_storage.hpp"

namespace hro::tab5::app {

static int level_db_to_y(float db);
static int hro_center_bin( void );
static uint16_t waterfall_color(float db);
static float hro_max_level_db( const float *spectrum );

void log_battery_status( void )
{
    const int32_t level   = M5.Power.getBatteryLevel();
    const int16_t voltage = M5.Power.getBatteryVoltage();
    const int32_t current = M5.Power.getBatteryCurrent();
    const bool charging   = M5.Power.isCharging();

    ESP_LOGI( TAG, "BAT: level=%ld%% voltage=%d mV current=%ld mA charging=%d", static_cast<long>( level ), static_cast<int>( voltage ), static_cast<long>( current ), charging ? 1 : 0 );
}

void draw_battery_status( void )
{
    const int32_t level   = M5.Power.getBatteryLevel();
    const int16_t voltage = M5.Power.getBatteryVoltage();
    const int32_t current = M5.Power.getBatteryCurrent();
    const bool charging   = M5.Power.isCharging();

    char buf[64];
    snprintf( buf, sizeof( buf ), "BAT %ld%%  %dmV  %ldmA%s", static_cast<long>( level ), static_cast<int>( voltage ), static_cast<long>( current ), charging ? " CHG" : "" );

    // 動作確認用表示領域
    constexpr int X = 764;
    constexpr int Y = 682;
    constexpr int W = 220;
    constexpr int H = 18;

    // 前回表示を消す
    M5.Display.fillRect( X, Y, W, H, DARKGREEN );

    M5.Display.setTextSize( 1 );
    M5.Display.setTextColor( WHITE );
    M5.Display.setTextDatum( middle_right );

    M5.Display.drawString( buf, X + W - 5, Y + H / 2 );
}

void draw_waterfall_time_axis( time_t axis_end )
{
    const int axis_y = WF_Y - 5;
    const int text_y = WF_Y - 22;

    M5.Display.fillRect( WF_X, WF_Y - 24, WF_W, 24, BLACK );

    time_t now;
    if ( axis_end != 0 ) {
        now = axis_end;
    } else {
        time( &now );
    }

    M5.Display.setFont( &fonts::Font0 );
    M5.Display.setTextSize( 1 );
    M5.Display.setTextColor( CYAN, BLACK );
    M5.Display.setTextDatum( top_center );

    // 目盛の時刻は偶数分に固定し、現在時刻との差をX座標へ変換する。
    // 1秒経過するごとに同じ目盛が1px左へ移動する。
    const int64_t axis_end_seconds = static_cast<int64_t>( now );
    for ( int64_t tick = hro::plot::latestTimeTick( axis_end_seconds );
          tick >= axis_end_seconds - TOTAL_SEC; tick -= INTERVAL_SEC ) {
        const int x = hro::plot::timeTickX( tick, axis_end_seconds );
        const time_t t = static_cast<time_t>( tick );
        struct tm tm_local;
        localtime_r( &t, &tm_local );
        char buf[16];
        snprintf( buf, sizeof( buf ), "%02d:%02d", tm_local.tm_hour, tm_local.tm_min );

        M5.Display.drawFastVLine( std::min( x, WF_X + WF_W - 1 ), axis_y, 5, CYAN );
        const int text_width = M5.Display.textWidth( buf );
        const int text_x = std::clamp( x - text_width / 2, WF_X,
                                      WF_X + WF_W - text_width );
        M5.Display.setTextDatum( top_left );
        M5.Display.drawString( buf, text_x, text_y );
    }
}

static int level_db_to_y(float db)
{ return hro::plot::levelY(db); }

static int hro_center_bin( void )
{
    const int32_t min_hz = hro_fft_min_hz();
    int bin = static_cast<int>( g_hro_config.fft_center_hz - min_hz );

    if ( bin < 0 ) {
        bin = 0;
    } else if ( bin >= HRO_BIN_COUNT ) {
        bin = HRO_BIN_COUNT - 1;
    }
    return bin;
}

static uint16_t waterfall_color(float db)
{
    const auto color = hro::plot::waterfallColor(db);
    return M5.Display.color565(static_cast<uint8_t>(color.r),
                               static_cast<uint8_t>(color.g),
                               static_cast<uint8_t>(color.b));
}

void draw_waterfall_column( const float *spectrum )
{
    if ( spectrum == nullptr ) {
        return;
    }

    // Pi5と同じ1200秒 = 1200px。各秒を必ず1列描画する。
    g_waterfall.scroll( -1, 0 );
    for ( int y = 0; y < WF_H; ++y ) {
        // Pi5のlroundと同じ最近傍binへ丸める。
        const int bin = hro::plot::binForRow(y);
        const uint16_t color = waterfall_color( spectrum[bin] );
        g_waterfall.fillRect( WF_W - 1, y, 1, 1, color );
    }
    g_waterfall.pushSprite( WF_X, WF_Y );
}

static float hro_max_level_db( const float *spectrum )
{
    if ( spectrum == nullptr ) {
        return LEVEL_DB_MIN;
    }

    const int center_bin = hro_center_bin();
    const int range_hz = g_hro_config.level_average_range_hz;

    // 現在は 1 Hz/bin
    int first_bin = center_bin - range_hz;
    int last_bin  = center_bin + range_hz;

    if ( first_bin < 0 ) {
        first_bin = 0;
    }

    if ( last_bin >= HRO_BIN_COUNT ) {
        last_bin = HRO_BIN_COUNT - 1;
    }

    float max_db = LEVEL_DB_MIN;

    for ( int bin = first_bin; bin <= last_bin; ++bin ) {
        if ( spectrum[bin] > max_db ) {
            max_db = spectrum[bin];
        }
    }

    return max_db;
}

void draw_hro_level_history( void )
{
    if ( g_hro_history == nullptr ) {
        return;
    }

    // Sprite上をクリア
    g_level_graph.fillSprite( BLACK );

    const uint16_t count = g_hro_history_count;

    if ( count != 0 ) {

        const int first_x = ( HRO_HISTORY_SECONDS - count ) * LEVEL_W / HRO_HISTORY_SECONDS;

        for ( uint16_t h = 0; h < count; ++h ) {
            const float *spectrum = get_hro_history( h );
            if ( spectrum == nullptr ) {
                continue;
            }

            // この1秒の表示範囲内の最大dB
            const float db = hro_max_level_db( spectrum );

            // 1秒分が占めるX範囲
            const int x0 = first_x + static_cast<int>( static_cast<uint32_t>( h ) * LEVEL_W / HRO_HISTORY_SECONDS );
            const int x1 = first_x + static_cast<int>( static_cast<uint32_t>( h + 1 ) * LEVEL_W / HRO_HISTORY_SECONDS );
            const int bar_w = x1 - x0;
            if ( bar_w <= 0 ) {
                continue;
            }

            // dB値をバーの高さへ変換
            const int y = level_db_to_y( db );

            // Waterfallと同じdB→色変換
            const uint16_t color = waterfall_color( db );

            // 下端から上方向へバー表示
            g_level_graph.fillRect( x0, y, bar_w, LEVEL_H - y, color );
        }
    }

    // LCDへ一括転送
    g_level_graph.pushSprite( LEVEL_X, LEVEL_Y );
}

void draw_current_time( void )
{
    time_t now;
    struct tm timeinfo;

    // Tab5-HROは日本標準時で動作
    setenv( "TZ", "JST-9", 1 );
    tzset();

    time( &now );
    localtime_r( &now, &timeinfo );

    char buf[32];

    constexpr int X = 540;
    constexpr int Y = 678;
    constexpr int W = 210;
    constexpr int H = 25;

    const bool ntp_synced = g_ntp_synced.load( std::memory_order_relaxed );
    // NTP同期済み = 青
    // RTC動作中   = オレンジ
    uint16_t bg_color;

    if ( ntp_synced ) {
        strftime( buf, sizeof( buf ), "%Y/%m/%d  %H:%M:%S [NTP]", &timeinfo );
        bg_color = M5.Display.color565( 0, 60, 120 );
    } else {
        strftime( buf, sizeof( buf ), "%Y/%m/%d  %H:%M:%S [RTC]", &timeinfo );
        bg_color = M5.Display.color565( 160, 80, 0 );
    }

    // 前の表示を背景色で消去
    M5.Display.fillRect( X, Y, W, H, bg_color );

    M5.Display.setFont( &fonts::Font2 );
    M5.Display.setTextDatum( middle_center );
    M5.Display.setTextColor( 0xFFFFFF );
    M5.Display.drawString( buf, X + W / 2, Y + H / 2 );
}

bool load_base_screen( void )
{
    size_t png_size = 0;

    uint8_t *png_data = load_file_to_psram( HRO_BACKGROUND_BASE, &png_size );
    if ( png_data == nullptr ) {
        return false;
    }

    M5Canvas base_screen( &M5.Display );
    base_screen.setPsram( true );

    if ( base_screen.createSprite( 1280, 720 ) == nullptr ) {
        ESP_LOGE( TAG, "Failed to create base screen sprite" );
        free( png_data );
        return false;
    }

    base_screen.fillSprite( BLACK );

    // PSRAM上のPNGをSpriteへ展開
    base_screen.drawPng( png_data, png_size, 0, 0 );

    // 完成した1280x720画面をLCDへ一括転送
    base_screen.pushSprite( 0, 0 );

    // 観測用Spriteの範囲だけを描き直す。
    // 右側の周波数目盛と背景PNGのカラーバーは保持する。
    g_waterfall.pushSprite( WF_X, WF_Y );
    g_level_graph.pushSprite( LEVEL_X, LEVEL_Y );

    // 不要になったメモリを解放
    base_screen.deleteSprite();
    free( png_data );

    return true;
}

void draw_waterfall_frequency_axis( void )
{
    // Pi5と同じ100 Hz間隔、ラベル右端X=40。
    constexpr int TICK_HZ = hro::plot::FREQUENCY_TICK_HZ;
    M5.Display.setFont( &fonts::Font0 );
    M5.Display.setTextSize( 1 );
    M5.Display.setTextColor( CYAN, BLACK );
    M5.Display.setTextDatum( middle_right );
    const int min_hz = hro_fft_min_hz();
    const int max_hz = hro_fft_max_hz();
    for ( int hz = ( max_hz / TICK_HZ ) * TICK_HZ; hz >= min_hz; hz -= TICK_HZ ) {
        const int y = static_cast<int>( hro::plot::frequencyY( hz, g_hro_config.fft_center_hz ) );
        char buf[16];
        snprintf( buf, sizeof( buf ), "%d", hz );
        M5.Display.drawString( buf, hro::plot::AXIS_LABEL_RIGHT, y );
        M5.Display.drawFastHLine( hro::plot::LEFT_TICK_START, y, hro::plot::LEFT_TICK_END - hro::plot::LEFT_TICK_START, CYAN );
        M5.Display.drawFastHLine( hro::plot::RIGHT_TICK_START, y, hro::plot::RIGHT_TICK_END - hro::plot::RIGHT_TICK_START, CYAN );
    }

    // Pi5のレベル目盛りと同じ位置。
    const int levels[] = { 20, 10, 0, -10 };
    for ( int db : levels ) {
        const int y = LEVEL_Y + level_db_to_y( static_cast<float>( db ) );
        char buf[16];
        snprintf( buf, sizeof( buf ), db > 0 ? "+%d" : "%d", db );
        M5.Display.drawString( buf, hro::plot::AXIS_LABEL_RIGHT, y );
        M5.Display.drawFastHLine( hro::plot::LEFT_TICK_START, y, hro::plot::LEFT_TICK_END - hro::plot::LEFT_TICK_START, CYAN );
    }
}

void draw_station_info( void )
{
    // PNG上部の情報表示領域
    //   1: Observer / Location
    //   2: Longitude / Latitude
    //   3: Receiver / Antenna
    //   4: RF / FFT

    const uint32_t WHITE = 0xFFFFFF;
    const uint32_t CYAN  = 0x00E5FF;

    // --------------------------------------------------
    // Decimal degrees -> DMS
    // --------------------------------------------------

    DmsValue lon = longitude_to_dms( g_hro_config.longitude );
    DmsValue lat = latitude_to_dms( g_hro_config.latitude );

    char line1[128];
    char line2[128];

    M5.Display.setTextDatum( middle_left );
    M5.Display.setFont( &fonts::Font2 );
//	M5.Display.setTextSize( 1.2f );	// フォント 1.2倍

    // --------------------------------------------------
    // 1. Observer / Location
    // x ≈ 490 .. 743
    // --------------------------------------------------

    snprintf( line1, sizeof( line1 ), "%s", g_hro_config.observer );
    snprintf( line2, sizeof( line2 ), "%s", g_hro_config.location );

    M5.Display.setTextColor( WHITE );
    M5.Display.drawString( line1, 474, 31 );

    M5.Display.setTextColor( CYAN );
    M5.Display.drawString( line2, 474, 58 );

    // --------------------------------------------------
    // 2. Longitude / Latitude
    // x ≈ 745 .. 892
    // --------------------------------------------------

    snprintf( line1, sizeof( line1 ),
             "%c %d %02d %05.2f",
             lon.direction,
             lon.degrees,
             lon.minutes,
             lon.seconds);

    snprintf( line2, sizeof( line2 ),
             "%c %d %02d %05.2f",
             lat.direction,
             lat.degrees,
             lat.minutes,
             lat.seconds);

    M5.Display.setTextColor( WHITE );
    M5.Display.drawString( line1, 780, 31 );
    M5.Display.drawString( line2, 780, 58 );


    // --------------------------------------------------
    // 3. Receiver / Antenna
    // x ≈ 894 .. 1108
    // --------------------------------------------------

    snprintf( line1, sizeof( line1 ), "%s", g_hro_config.receiver );
    snprintf(line2, sizeof( line2 ), "%s", g_hro_config.antenna );

    M5.Display.setTextColor( WHITE );
    M5.Display.drawString( line1, 905, 31 );

    M5.Display.setTextColor( CYAN );
    M5.Display.drawString( line2, 905, 58 );

    // Gain uses the same header column as Pi5. Small font fits the 70px gap.
    M5.Display.setFont( &fonts::Font0 );
    M5.Display.setTextSize( 1 );
    M5.Display.setTextDatum( top_left );
    snprintf( line1, sizeof( line1 ), "%d.%d dB",
              g_hro_config.sdr_gain / 10, g_hro_config.sdr_gain % 10 );
    M5.Display.setTextColor( WHITE );
    M5.Display.drawString( line1, hro::plot::STATION_GAIN_X, 28 );
    M5.Display.setFont( &fonts::Font2 );
    M5.Display.setTextDatum( middle_left );

    // --------------------------------------------------
    // 4. RF / FFT
    // x ≈ 1110 .. 1268
    // --------------------------------------------------

    const double rf_mhz = static_cast<double>( g_hro_config.frequency_hz ) / 1000000.0;
    snprintf( line1, sizeof( line1 ), "%.6f MHz", rf_mhz );
    snprintf( line2, sizeof( line2 ), "FFT %ld +/-%ld Hz", static_cast<long>( g_hro_config.fft_center_hz ),static_cast<long>( g_hro_config.fft_range_hz ) );

    M5.Display.setTextColor( WHITE );
    M5.Display.drawString( line1, 1130, 31 );

    M5.Display.setTextColor( CYAN );
    M5.Display.drawString( line2, 1130, 58 );
}

} // namespace hro::tab5::app
