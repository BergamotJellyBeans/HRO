#include "tab5_helpers.hpp"
#include "tab5_frontend.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hro::tab5::app {

static int hex_value( char c );

DmsValue longitude_to_dms( double value )
{
    DmsValue dms = {};

    dms.direction = ( value < 0.0 ) ? 'W' : 'E';
    double v = fabs( value );
    dms.degrees = static_cast<int>( v );
    double m = ( v - dms.degrees ) * 60.0;
    dms.minutes = static_cast<int>( m );
    dms.seconds = ( m - dms.minutes ) * 60.0;

    return dms;
}

DmsValue latitude_to_dms( double value )
{
    DmsValue dms = {};

    dms.direction = ( value < 0.0 ) ? 'S' : 'N';
    double v = fabs( value );
    dms.degrees = static_cast<int>( v );
    double m = ( v - dms.degrees ) * 60.0;
    dms.minutes = static_cast<int>( m );
    dms.seconds = ( m - dms.minutes ) * 60.0;

    return dms;
}

static int hex_value( char c )
{
    if ( c >= '0' && c <= '9' ) return c - '0';
    if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
    return -1;
}

void url_decode( char *dst, size_t dst_size, const char *src )
{
    if ( dst_size == 0 ) {
        return;
    }

    size_t di = 0;
    while ( *src != '\0' && di < dst_size - 1 ) {
        if ( *src == '+' ) {
            dst[di++] = ' ';
            src++;
        } else if ( *src == '%' && src[1] != '\0' && src[2] != '\0' ) {
            int hi = hex_value( src[1] );
            int lo = hex_value( src[2] );
            if ( hi >= 0 && lo >= 0 ) {
                dst[di++] = static_cast<char>( ( hi << 4 ) | lo );
                src += 3;
            } else {
                dst[di++] = *src++;
            }
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

bool get_form_value( const char *body, const char *name, char *out, size_t out_size )
{
    if ( !body || !name || !out || out_size == 0 ) {
        return false;
    }

    char key[64];
    snprintf( key, sizeof( key ), "%s=", name );

    const char *p = strstr( body, key );
    while ( p ) {
        // 先頭、または & の直後だけをキーとして認識
        if ( p == body || *( p - 1 ) == '&' ) {
            break;
        }
        p = strstr( p + 1, key );
    }

    if ( !p ) {
        return false;
    }

    p += strlen( key );

    const char *end = strchr( p, '&' );
    size_t len = end ? static_cast<size_t>( end - p ): strlen( p );

    // URLエンコード状態の一時バッファ
    char encoded[256];

    if ( len >= sizeof( encoded ) ) {
        len = sizeof( encoded ) - 1;
    }

    memcpy( encoded, p, len );
    encoded[len] = '\0';

    // 既存のURLデコード関数を使用
    url_decode( out, out_size, encoded );

    return true;
}

bool point_in_rect( int x, int y, int rx, int ry, int rw, int rh )
{
    return x >= rx &&
           x <  rx + rw &&
           y >= ry &&
           y <  ry + rh;
}

bool format_png_filename(const char* prefix, time_t block_start, char* filename, size_t filename_size)
{
    if ( filename == nullptr || filename_size == 0 ) {
        return false;
    }

    if ( !is_valid_file_prefix( prefix ) ) {
        return false;
    }

    // block_start は Unix time (UTC)。
    // ファイル名はJSTで生成する。
    const time_t jst_timestamp = block_start + 9 * 60 * 60;

    struct tm jst_tm;
    gmtime_r( &jst_timestamp, &jst_tm );

    const int ret = snprintf(
        filename,
        filename_size,
        "%s%04d%02d%02d%02d%02d.png",
        prefix,
        jst_tm.tm_year + 1900,
        jst_tm.tm_mon + 1,
        jst_tm.tm_mday,
        jst_tm.tm_hour,
        jst_tm.tm_min
    );

    return ret > 0 && static_cast<size_t>( ret ) < filename_size;
}

HroTuning calculate_tuning(const Tab5Config& config)
{
    HroTuning t = {};

    t.target_rf_hz  = config.frequency_hz;
    t.fft_center_hz = config.fft_center_hz;

    // ------------------------------------------------------------
    // TEST 3: rtl_fm方式 Fs/4 DC回避
    //
    // Fs = 256 kHz
    // Fs/4 = 64 kHz
    //
    // 目的RFをRTL-SDR LOの -64 kHz に置く。
    // raw IQ側で +Fs/4 rotate を行いDCへ戻す。
    // その後、既存NCOでFFT中心 780 Hzへ移動する。
    // ------------------------------------------------------------

    static constexpr uint32_t FS4_HZ = hro::tab5::INPUT_RATE / 4U;   // hro::tab5::INPUT_RATE/ 4 Hz

    t.ideal_lo_hz = t.target_rf_hz + FS4_HZ;
    t.actual_lo_hz = ( t.ideal_lo_hz / 1000U ) * 1000U;

    // rotate前の実IF
    // 53.372000 - 53.436000 = -64000 Hz
    t.actual_if_hz = static_cast<int32_t>( t.target_rf_hz - t.actual_lo_hz );

    // Fs/4 rotate後は目的信号がほぼ0 Hzになるので、
    // 既存NCOではFFT中心まで +780 Hz移動。
    //
    // Fs/4回転と組み合わせて使用する。
    const int32_t if_after_fs4 = t.actual_if_hz + static_cast<int32_t>( FS4_HZ );

    t.nco_shift_hz = static_cast<float>( t.fft_center_hz - if_after_fs4 );


    return t;
}

size_t history_physical_index(size_t index, size_t count, size_t write_position, size_t capacity)
{
    if (capacity == 0 || index >= count || count > capacity) return capacity;
    return count < capacity ? index : (write_position + index) % capacity;
}

} // namespace hro::tab5::app
