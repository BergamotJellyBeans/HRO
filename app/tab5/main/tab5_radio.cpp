#include "tab5_runtime.hpp"
#include "tab5_radio.hpp"
#include "tab5_helpers.hpp"
#include "tab5_observation.hpp"

namespace hro::tab5::app {
static void apply_sdr_gain(int gain)
{
    const esp_err_t err = esp_rtl_sdr_set_tuner_gain(g_rtl, gain);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTL tuner gain requested: %.1f dB (manual)", gain / 10.0);
    } else {
        ESP_LOGE(TAG, "RTL gain request failed: %s", esp_rtl_sdr_err_to_name(err));
    }
}

static std::atomic<bool> g_rtl_ready{false};
static std::atomic<bool> g_stream_started{false};
static std::atomic<uint32_t> g_interval_bytes{0};
static std::atomic<uint32_t> g_interval_blocks{0};
static std::atomic<uint32_t> g_pipeline_drops{0};


HroTuning make_hro_tuning( void )
{ return calculate_tuning(g_hro_config); }

void rtl_event_callback( esp_rtl_sdr_event_t event, const void *payload, void *user_ctx )
{
    (void)user_ctx;

    switch ( event ) {
    case ESP_RTL_SDR_EVT_ENUMERATED: {
        const auto *info = static_cast<const esp_rtl_sdr_device_info_t *>( payload );
        if ( info != nullptr ) {
            ESP_LOGI( TAG, "RTL enumerated VID=%04x PID=%04x serial=%s USB=%s",
                     info->vid,
                     info->pid,
                     info->serial,
                     info->high_speed ? "HIGH" : "FULL" );
        } else {
            ESP_LOGI( TAG, "RTL enumerated" );
        }
        break;
    }

    case ESP_RTL_SDR_EVT_READY:
        ESP_LOGI( TAG, "RTL READY" );
        g_rtl_ready.store( true, std::memory_order_release );
        break;

    case ESP_RTL_SDR_EVT_STREAM_STARTED:
        ESP_LOGI( TAG, "RTL STREAM STARTED" );
        g_stream_started.store( true, std::memory_order_release );
        break;

case ESP_RTL_SDR_EVT_IQ_BLOCK: {
    const auto *iq = static_cast<const esp_rtl_sdr_iq_block_t *>( payload );
    if ( iq == nullptr || iq->data == nullptr || iq->bytes == 0 ) {
        break;
    }

    // RTL-SDRから受け取った量
    g_interval_bytes.fetch_add( static_cast<uint32_t>( iq->bytes ), std::memory_order_relaxed );
    g_interval_blocks.fetch_add( 1, std::memory_order_relaxed );

    uint8_t slot = 0;

    // 空きslotを取得
    if ( g_free_q == nullptr || g_filled_q == nullptr || xQueueReceive( g_free_q, &slot, 0 ) != pdTRUE ) {
        g_pipeline_drops.fetch_add( 1, std::memory_order_relaxed );
        break;
    }

    // 念のためサイズ確認
    if ( iq->bytes > IQ_SLOT_BYTES ) {
        g_pipeline_drops.fetch_add( 1, std::memory_order_relaxed );
        (void)xQueueSend( g_free_q, &slot, 0 );
        break;
    }

    // esp_rtl_sdrのborrowed bufferから
    // 自前PSRAMへ即コピー
    std::memcpy( g_iq_slots[slot], iq->data, iq->bytes );

    RtlIqBlock block {
        g_iq_slots[slot],
        iq->bytes,
        slot
    };

    // DSP taskへ渡す
    if ( xQueueSend( g_filled_q, &block, 0 ) != pdTRUE ) {
        g_pipeline_drops.fetch_add( 1, std::memory_order_relaxed );

        // 失敗したらslotを返却
        (void)xQueueSend( g_free_q, &slot, 0 );
        }
        break;
    }
    case ESP_RTL_SDR_EVT_STOPPED:
        ESP_LOGW( TAG, "RTL STOPPED" );
        g_stream_started.store( false, std::memory_order_release );
        break;

    case ESP_RTL_SDR_EVT_DISCONNECTED:
        ESP_LOGW( TAG, "RTL DISCONNECTED" );
        g_rtl_ready.store( false, std::memory_order_release );
        g_stream_started.store( false, std::memory_order_release );
        break;

    case ESP_RTL_SDR_EVT_RETUNED: {
        const auto *hz = static_cast<const uint32_t *>( payload );
        if ( hz != nullptr ) {
            ESP_LOGI( TAG, "RTL RETUNED: %u Hz", static_cast<unsigned>( *hz ) );
        }
        break;
    }

    case ESP_RTL_SDR_EVT_ERROR: {
        const auto *err = static_cast<const esp_rtl_sdr_error_info_t *>( payload );
        ESP_LOGE( TAG, "RTL ERROR: %s", err != nullptr ? esp_rtl_sdr_err_to_name( err->code ) : "unknown" );
        break;
    }

    default:
        break;
    }
}

void hro_radio_task( void * )
{
    ESP_LOGI( TAG, "Waiting for RTL-SDR READY..." );

    while ( !g_rtl_ready.load( std::memory_order_acquire ) ) {
        vTaskDelay( pdMS_TO_TICKS( 100 ) );
    }

    ESP_LOGI( TAG, "RTL ready" );

    if ( !esp_rtl_sdr_is_rate_supported( RTL_SAMPLE_RATE ) ) {
        ESP_LOGE( TAG, "Sample rate %u not supported", static_cast<unsigned>( RTL_SAMPLE_RATE ) );
        vTaskDelete( nullptr );
        return;
    }

    esp_rtl_sdr_stream_config_t stream;
    esp_rtl_sdr_stream_config_default( &stream );

    stream.preset = ESP_RTL_SDR_PRESET_CUSTOM_HZ;
    stream.frequency_hz = g_hro_lo_frequency_hz;
    stream.sample_rate_sps = RTL_SAMPLE_RATE;

    // 連続受信
    stream.max_bytes = 0;
    stream.timeout_ms = 0;

    esp_err_t err = esp_rtl_sdr_stream_config_validate( &stream );

    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "stream config error: %s", esp_rtl_sdr_err_to_name( err ) );
        vTaskDelete( nullptr );
        return;
    }

    ESP_LOGI(TAG,
             "Starting stream RF=%u Hz rate=%u sps",
             static_cast<unsigned>( g_hro_lo_frequency_hz ),
             static_cast<unsigned>( RTL_SAMPLE_RATE ) );

    err = esp_rtl_sdr_start( g_rtl, &stream );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "RTL start failed: %s", esp_rtl_sdr_err_to_name( err ) );
        vTaskDelete( nullptr );
        return;
    }

    apply_sdr_gain(g_hro_config.sdr_gain);

    ESP_LOGI(TAG, "HRO IQ stream running");

    uint32_t log_seconds = 0;
    int64_t resamp_test_start_us = esp_timer_get_time();

    while ( true ) {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
        const uint32_t rx_bytes = g_interval_bytes.exchange( 0, std::memory_order_relaxed );
        const uint32_t rx_blocks = g_interval_blocks.exchange( 0, std::memory_order_relaxed );
        const uint32_t dsp_bytes = g_dsp_interval_bytes.exchange( 0, std::memory_order_relaxed );
        const uint32_t dsp_blocks = g_dsp_interval_blocks.exchange( 0, std::memory_order_relaxed );
        const uint32_t drops = g_pipeline_drops.load( std::memory_order_relaxed );
        const uint32_t decim_samples = g_decim_interval_samples.exchange(0, std::memory_order_relaxed );
        const uint32_t resamp_samples = g_resamp_interval_samples.exchange( 0, std::memory_order_relaxed );
        const uint32_t fft_frames = g_fft_frames.exchange( 0, std::memory_order_relaxed );
        const uint32_t fft_processed = g_fft_processed.exchange( 0, std::memory_order_relaxed );
        const uint32_t fft_drops = g_fft_drops.load( std::memory_order_relaxed );
        ESP_LOGI(
            TAG,
            "RX=%u B/s %u blk  "
            "DSP=%u B/s %u blk  "
            "DECIM=%u  RESAMP=%u  "
            "FFT=%u/%u  "
            "drops=%u fftdrops=%u",
            rx_bytes,
            rx_blocks,
            dsp_bytes,
            dsp_blocks,
            decim_samples,
            resamp_samples,
            fft_frames,
            fft_processed,
            drops,
            fft_drops );

        if ( ++log_seconds >= 10 ) {
            log_seconds = 0;
            const int64_t now_us = esp_timer_get_time();
            const uint32_t count = g_resamp_10sec_samples.exchange( 0, std::memory_order_relaxed );
            const double elapsed_sec = static_cast<double>( now_us - resamp_test_start_us ) / 1000000.0;
            const double actual_rate = count / elapsed_sec;

            ESP_LOGI( TAG, "RESAMP test: %u samples / %.3f sec = %.2f sps", count, elapsed_sec, actual_rate );
            resamp_test_start_us = now_us;
        }
        if ( g_hro_history_count >= 10 ) {
            const float *oldest = get_hro_history( 0 );
            const float *newest = get_hro_history( g_hro_history_count - 1 );
            if ( oldest != nullptr && newest != nullptr ) {
                ESP_LOGI(
                TAG,
                "HISTORY READ: count=%u  "
                "oldest780=%.1f  newest780=%.1f",
                static_cast<unsigned>(g_hro_history_count), oldest[780 - HRO_BIN_MIN], newest[780 - HRO_BIN_MIN] );
            }
        }
    }
}

} // namespace hro::tab5::app
