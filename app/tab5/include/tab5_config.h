#pragma once

#include <cstddef>
#include <cstdint>

//------------------------------------------------------------------------------
// Tab5Config
//
// Tab5-HRO の観測・受信・表示・Audio・Screenshot設定。
// 設定内容は SDカード上の config.ini に保存する。
//------------------------------------------------------------------------------
struct Tab5Config
{
    // Station information
    char observer[64];
    char location[128];

    double latitude;
    double longitude;

    // Receiver information
    char receiver[64];
    char antenna[128];

    // Receiver / FFT settings
    int sdr_gain;                 // 0.1 dB units, manual tuner gain
    uint32_t frequency_hz;
    int32_t  fft_center_hz;
    int32_t  fft_range_hz;
    int32_t  level_average_range_hz;

    // Pi5 display terminal (IPv4, empty means not configured)
    char pi5_address[16];
    char source_system_info[128]; // Remote display text; never written to config.ini.

    // Local waterfall brightness offset (dB), independent of observation data.
    int display_level_db;

    // Audio
    int  audio_volume;
    bool audio_mute;

    // Screenshot
    char screenshot_prefix[9];     // 最大8文字 + '\0'
};

//------------------------------------------------------------------------------
// HroTuning
//
// 観測RFとRTL-SDRの実際のLOから、DSPで必要となる周波数シフトを保持する。
//------------------------------------------------------------------------------
struct HroTuning
{
    uint32_t target_rf_hz;         // 観測対象RF
    int32_t  fft_center_hz;        // FFT上の表示中心周波数
    uint32_t ideal_lo_hz;          // 理想LO
    uint32_t actual_lo_hz;         // RTL-SDRへ設定する1 kHz単位LO
    int32_t  actual_if_hz;         // target RF - actual LO
    float    nco_shift_hz;         // FFT中心へ合わせるデジタルシフト
};

//------------------------------------------------------------------------------
// Global configuration
//------------------------------------------------------------------------------
extern Tab5Config g_hro_config;
Tab5Config& stored_hro_config();
void preserve_standalone_config();

//------------------------------------------------------------------------------
// Configuration file paths
//------------------------------------------------------------------------------
extern const char *const HRO_CONFIG_DIR;
extern const char *const HRO_CONFIG_FILE;
extern const char *const HRO_CONFIG_TEMP_FILE;

//------------------------------------------------------------------------------
// Configuration helpers
//------------------------------------------------------------------------------
int32_t hro_fft_min_hz( void );
int32_t hro_fft_max_hz( void );
int32_t hro_fft_bin_count( void );

bool is_valid_tab5_sdr_gain(int gain);

bool is_valid_file_prefix( const char *prefix );

bool validate_hro_config(
    const Tab5Config &cfg,
    char *error_msg,
    size_t error_msg_size,
    bool allow_pi5_gain = false
);

bool write_hro_config_file(
    const char *filename,
    const Tab5Config &cfg
);

bool save_hro_config( void );
bool load_hro_config( void );
bool ensure_hro_config( void );
