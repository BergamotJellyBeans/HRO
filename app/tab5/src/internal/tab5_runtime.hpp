#pragma once
// Internal task exchange only. Each module owns its other state privately.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <sys/time.h>
#include <sys/stat.h>
#include <cmath>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include <dirent.h>

#include <M5Unified.h>
#include "esp_rtl_sdr.h"
#include "esp_timer.h"
#include "dsps_fft2r.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "nvs.h"
#include "esp_sntp.h"

#include "tab5_config.h"
#include "hro_plot.h"
#include "dsp/nco_shifter.h"
#include "dsp/hann_window.h"
#include "dsp/spectrum_math.h"
#include "dsp/resampler_16_125.h"
#include "tab5_frontend.h"
#include "tab5_helpers.hpp"

namespace hro::tab5::app {
struct RtlIqBlock {
    uint8_t *data;
    size_t bytes;
    uint8_t slot;
};

struct ComplexSample {
    float i;
    float q;
};

struct FftFrame {
    float *i;
    float *q;
    uint8_t slot;
};

inline const char *TAG = "Tab5-HRO";

inline constexpr uint32_t RTL_SAMPLE_RATE = hro::tab5::INPUT_RATE;

static_assert( RTL_SAMPLE_RATE == 256000, "Tab5 uses 256 kS/s" );

inline constexpr size_t IQ_SLOT_BYTES = 32768;

inline constexpr uint8_t IQ_SLOT_COUNT = 32;

inline constexpr float HRO_SAMPLE_RATE = static_cast<float>( RTL_SAMPLE_RATE );

inline constexpr bool HRO_TEST_TONE = false;

inline constexpr float HRO_TEST_TONE_HZ = 1000.0f;

inline constexpr float TWO_PI = 6.2831853071795864769f;

inline const char *SD_MOUNT_POINT = "/sdcard";

inline constexpr size_t DECIM_BUFFER_SAMPLES = 1024;

inline constexpr size_t FFT_SIZE = hro::plot::FFT_SIZE;

inline constexpr int FFT_BUFFER_COUNT = 2;

inline constexpr int INTERVAL_SEC = hro::plot::TIME_TICK_SECONDS;

inline constexpr int TOTAL_SEC    = hro::plot::SECONDS;

inline constexpr int HRO_BIN_MIN = 780 - hro::FFT_RANGE_HZ;

inline constexpr int HRO_BIN_MAX = 780 + hro::FFT_RANGE_HZ;

inline constexpr int HRO_BIN_COUNT = hro::FFT_BIN_COUNT;

inline constexpr int HRO_HISTORY_SECONDS = TOTAL_SEC;

inline constexpr int WF_X = hro::plot::LEFT;

inline constexpr int WF_Y = hro::plot::WATERFALL_TOP;

inline constexpr int WF_W = hro::plot::WIDTH;

inline constexpr int WF_H = hro::plot::WATERFALL_HEIGHT;

inline constexpr int SPEC_X = 35;

inline constexpr int SPEC_Y = 521;

inline constexpr int SPEC_W = 966;

inline constexpr int SPEC_H = 127;

inline constexpr int LEVEL_X = WF_X;

inline constexpr int LEVEL_Y = hro::plot::LEVEL_TOP;

inline constexpr int LEVEL_W = WF_W;

inline constexpr int LEVEL_H = hro::plot::LEVEL_HEIGHT;

static_assert( WF_W == TOTAL_SEC, "Waterfall requires one pixel per second" );

static_assert( LEVEL_Y == 370 && LEVEL_Y + LEVEL_H == 450, "Pi5 level geometry must match" );

inline constexpr float LEVEL_DB_MIN = -10.0f;

inline constexpr float LEVEL_DB_MAX =  30.0f;

inline constexpr int SHUTDOWN_X = 1015;

inline constexpr int SHUTDOWN_Y = 480;

inline constexpr int SHUTDOWN_W = 250;

inline constexpr int SHUTDOWN_H = 32;

inline constexpr int CAP_X = 0;

inline constexpr int CAP_Y = 0;

inline constexpr int CAP_W = hro::plot::IMAGE_WIDTH;

inline constexpr int CAP_H = hro::plot::IMAGE_HEIGHT;

inline constexpr int WF_SPRITE_Y    = 0;

inline constexpr int LEVEL_SPRITE_Y = WF_H;

inline constexpr int WF_SPRITE_H = WF_H + LEVEL_H;

inline constexpr const char *HRO_BACKGROUND_BASE = "/sdcard/radio_meteor_observation_base_1280x720.png";

inline constexpr uint32_t AUDIO_SAMPLE_RATE   = 8192;

inline constexpr size_t   AUDIO_BLOCK_SAMPLES = 256;

struct AudioBlock
{
    int16_t samples[AUDIO_BLOCK_SAMPLES];
};

inline constexpr int      AUDIO_BUFFER_COUNT  = 3;

inline constexpr int AUDIO_VOL_MINUS_X = 1025;

inline constexpr int AUDIO_VOL_MINUS_Y = 557;

inline constexpr int AUDIO_VOL_MINUS_W = 62;

inline constexpr int AUDIO_VOL_MINUS_H = 38;

inline constexpr int AUDIO_VOL_PLUS_X = 1193;

inline constexpr int AUDIO_VOL_PLUS_Y = 557;

inline constexpr int AUDIO_VOL_PLUS_W = 62;

inline constexpr int AUDIO_VOL_PLUS_H = 38;

inline constexpr int AUDIO_MUTE_X = 1080;

inline constexpr int AUDIO_MUTE_Y = 605;

inline constexpr int AUDIO_MUTE_W = 120;

inline constexpr int AUDIO_MUTE_H = 34;

inline constexpr int64_t SHUTDOWN_HOLD_US = 2 * 1000 * 1000LL;

extern uint32_t g_hro_lo_frequency_hz;

extern esp_rtl_sdr_handle_t g_rtl;

extern uint8_t *g_iq_slots[IQ_SLOT_COUNT];

extern QueueHandle_t g_free_q;

extern QueueHandle_t g_filled_q;

extern std::atomic<uint32_t> g_dsp_interval_bytes;

extern std::atomic<uint32_t> g_dsp_interval_blocks;

extern float g_hro_freq_shift;

extern std::atomic<uint32_t> g_decim_interval_samples;

extern std::atomic<uint32_t>g_resamp_interval_samples;

extern std::atomic<uint32_t> g_resamp_10sec_samples;

extern float *g_fft_i[FFT_BUFFER_COUNT];

extern float *g_fft_q[FFT_BUFFER_COUNT];

extern QueueHandle_t g_fft_free_q;

extern QueueHandle_t g_fft_ready_q;

extern uint8_t g_fft_write_slot;

extern size_t g_fft_pos;

extern std::atomic<uint32_t> g_fft_frames;

extern std::atomic<uint32_t> g_fft_processed;

extern std::atomic<uint32_t> g_fft_drops;

extern float *g_fft_work;

extern float g_hro_spectrum[HRO_BIN_COUNT];

extern std::atomic<uint32_t> g_hro_spectrum_sequence;

extern float *g_hro_history;

extern uint16_t g_hro_history_write_pos;

extern uint16_t g_hro_history_count;

extern std::atomic<bool> g_ntp_synced;

extern QueueHandle_t g_audio_queue;

extern std::atomic<bool> g_audio_mute;

extern std::atomic<int>  g_audio_volume;

extern std::atomic<bool> g_audio_config_dirty;

extern std::atomic<bool> g_hro_shutdown_requested;

extern std::atomic<bool> g_hro_storage_stopped;

extern M5Canvas g_waterfall;

extern M5Canvas g_level_graph;
}
