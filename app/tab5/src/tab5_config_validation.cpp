#include "tab5_config.h"
#include "hro_fft_config.h"
#include "hro_sdr_config.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>

bool is_valid_tab5_sdr_gain(int gain)
{
    // The pinned Tab5 driver has no measured 48.0 dB step.
    return gain != 480 && std::find(hro::SDR_GAIN_VALUES.begin(),
        hro::SDR_GAIN_VALUES.end(), gain) != hro::SDR_GAIN_VALUES.end();
}

bool is_valid_file_prefix( const char *prefix )
{
    if ( prefix == nullptr ) {
        return false;
    }

    const size_t len = strlen( prefix );

    if ( len < 1 || len > 8 ) {
        return false;
    }

    for ( size_t i = 0; i < len; ++i ) {
        const char c = prefix[i];

        if ( !( ( c >= 'A' && c <= 'Z' ) ||
                ( c >= 'a' && c <= 'z' ) ||
                ( c >= '0' && c <= '9' ) ) ) {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
// validate_hro_config
//
// HRO設定値が現在のDSP・表示条件の範囲内か検証する。
//------------------------------------------------------------------------------
bool validate_hro_config( const Tab5Config &cfg, char *error_msg, size_t error_msg_size, bool allow_pi5_gain )
{
    auto set_error = [&]( const char *msg ) {
        if ( error_msg && error_msg_size > 0 ) {
            snprintf( error_msg, error_msg_size, "%s", msg );
        }

        return false;
    };

    if (cfg.pi5_address[0]) {
        unsigned a, b, c, d; char extra;
        if (sscanf(cfg.pi5_address, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4 ||
            a == 0 || a >= 224 || b > 255 || c > 255 || d > 255)
            return set_error("Pi5 address must be a unicast IPv4 address");
        for (const char* p = cfg.pi5_address; *p; ++p)
            if (!(*p >= '0' && *p <= '9') && *p != '.')
                return set_error("Invalid Pi5 IPv4 address");
    }

    // Station position
    if ( cfg.latitude < -90.0 || cfg.latitude > 90.0 ) {
        return set_error( "Latitude must be between -90 and +90 degrees" );
    }

    if ( cfg.longitude < -180.0 || cfg.longitude > 180.0 ) {
        return set_error( "Longitude must be between -180 and +180 degrees" );
    }

    if (!is_valid_tab5_sdr_gain(cfg.sdr_gain) && !(allow_pi5_gain && cfg.sdr_gain == 480)) {
        return set_error("Select a supported SDR gain");
    }

    // Receiving frequency
    if ( cfg.frequency_hz < 1000000UL || cfg.frequency_hz > 2000000000UL ) {
        return set_error( "Receiving frequency must be between 1 MHz and 2000 MHz" );
    }

    // FFT settings
    if ( cfg.fft_center_hz < 0 ) {
        return set_error( "FFT center frequency must be 0 Hz or greater" );
    }

    if ( cfg.fft_range_hz != hro::FFT_RANGE_HZ ) {
        return set_error( "FFT display range must be 300 Hz" );
    }

    const int64_t fft_min = static_cast<int64_t>( cfg.fft_center_hz ) - static_cast<int64_t>( cfg.fft_range_hz );
    const int64_t fft_max = static_cast<int64_t>( cfg.fft_center_hz ) + static_cast<int64_t>( cfg.fft_range_hz );

    if ( fft_min < 0 ) {
        return set_error( "FFT display lower frequency must be 0 Hz or greater" );
    }

    // 現在のresampler LPFは約1800 Hz
    if ( fft_max > 1800 ) {
        return set_error( "FFT display upper frequency must not exceed 1800 Hz" );
    }

    // 現在は1 Hz/bin
    const int64_t bin_count = fft_max - fft_min + 1;

    if ( bin_count < 1 || bin_count > 3601 ) {
        return set_error( "Invalid FFT display bin count" );
    }

    // Level Graph範囲
    if ( cfg.level_average_range_hz < 0 || cfg.level_average_range_hz > cfg.fft_range_hz ) {
        return set_error( "Level Peak Range must be between 0 and FFT Range." );
    }

    // Screenshot settings
    if ( !is_valid_file_prefix( cfg.screenshot_prefix ) ) {
        return set_error( "Screenshot File Prefix must be 1 to 8 alphanumeric characters" );
    }

    if ( error_msg && error_msg_size > 0 ) {
        error_msg[0] = '\0';
    }

    return true;
}

//------------------------------------------------------------------------------
// write_hro_config_file
//
// 指定されたTab5ConfigをINI形式で保存する。
//------------------------------------------------------------------------------
