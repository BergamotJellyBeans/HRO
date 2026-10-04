#include "tab5_runtime.hpp"

namespace hro::tab5::app {
uint32_t g_hro_lo_frequency_hz = 77399000;
esp_rtl_sdr_handle_t g_rtl = nullptr;
uint8_t *g_iq_slots[IQ_SLOT_COUNT] = {};
QueueHandle_t g_free_q = nullptr;
QueueHandle_t g_filled_q = nullptr;
std::atomic<uint32_t> g_dsp_interval_bytes{0};
std::atomic<uint32_t> g_dsp_interval_blocks{0};
float g_hro_freq_shift = -220.0f;
std::atomic<uint32_t> g_decim_interval_samples{0};
std::atomic<uint32_t>g_resamp_interval_samples{0};
std::atomic<uint32_t> g_resamp_10sec_samples{0};
float *g_fft_i[FFT_BUFFER_COUNT] = {};
float *g_fft_q[FFT_BUFFER_COUNT] = {};
QueueHandle_t g_fft_free_q = nullptr;
QueueHandle_t g_fft_ready_q = nullptr;
uint8_t g_fft_write_slot = 0;
size_t g_fft_pos = 0;
std::atomic<uint32_t> g_fft_frames{0};
std::atomic<uint32_t> g_fft_processed{0};
std::atomic<uint32_t> g_fft_drops{0};
float *g_fft_work = nullptr;
float g_hro_spectrum[HRO_BIN_COUNT];
std::atomic<uint32_t> g_hro_spectrum_sequence{0};
float *g_hro_history = nullptr;
uint16_t g_hro_history_write_pos = 0;
uint16_t g_hro_history_count = 0;
std::atomic<bool> g_ntp_synced{false};
QueueHandle_t g_audio_queue = nullptr;
std::atomic<bool> g_audio_mute{false};
std::atomic<int>  g_audio_volume{100};
std::atomic<int> g_display_level_db{0};
std::atomic<bool> g_audio_config_dirty{false};
std::atomic<bool> g_hro_shutdown_requested{ false };
std::atomic<bool> g_hro_storage_stopped{ false };
M5Canvas g_waterfall( &M5.Display );
M5Canvas g_level_graph( &M5.Display );
}
