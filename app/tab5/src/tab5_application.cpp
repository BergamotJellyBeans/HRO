#include "tab5_runtime.hpp"
#include "tab5_application.hpp"
#include "tab5_audio.hpp"
#include "tab5_controls.hpp"
#include "tab5_display.hpp"
#include "tab5_dsp.hpp"
#include "tab5_fft.hpp"
#include "tab5_observation.hpp"
#include "tab5_radio.hpp"
#include "tab5_storage.hpp"
#include "tab5_time.hpp"
#include "tab5_wifi.hpp"

namespace hro::tab5::app {



void start()
{
    ESP_LOGI( TAG, "" );
    ESP_LOGI( TAG, "==========================" );
    ESP_LOGI( TAG, "      Tab5-HRO" );
    ESP_LOGI( TAG, "==========================" );

    auto m5cfg = M5.config();
    M5.begin( m5cfg );

    M5.Speaker.begin();
    M5.Speaker.setVolume( 128 );

    M5.Power.setExtOutput( true, m5::ext_USB );
    M5.Display.setRotation( 3 );
    M5.Display.setBrightness( 200 );

//	show_system_ready();

    esp_err_t ret = nvs_flash_init();    // RTC → System Clock
    restore_system_time_from_rtc();

    if ( ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND ) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ESP_ERROR_CHECK( nvs_flash_init() );
    } else {
        ESP_ERROR_CHECK( ret );
    }

    char saved_ssid[33] = {};
    char saved_password[65] = {};

    if ( load_wifi_settings( saved_ssid, sizeof( saved_ssid ), saved_password, sizeof( saved_password ) ) ) {
        ESP_LOGI( TAG, "Wi-Fi configuration restored from NVS" );
    }

    g_waterfall.setPsram( true );
    g_waterfall.setColorDepth( 16 );
    if ( g_waterfall.createSprite( WF_W, WF_H ) == nullptr ) {
        ESP_LOGE( TAG, "Failed to create combined waterfall sprite" );
    } else {
        ESP_LOGI( TAG, "Combined sprite created: %d x %d", WF_W, WF_SPRITE_H );
        // Waterfall部分
        g_waterfall.fillRect( 0, WF_SPRITE_Y, WF_W, WF_H, M5.Display.color565( 0, 10, 18 ) );
        ESP_LOGI( TAG, "Waterfall sprite created: %d x %d", WF_W, WF_H );
    }

    g_level_graph.setPsram( false );
    g_level_graph.setColorDepth( 8 );
    if ( g_level_graph.createSprite( LEVEL_W, LEVEL_H ) == nullptr ) {
        ESP_LOGE( TAG, "Level graph sprite allocation failed" );
    } else {
        g_level_graph.fillSprite( BLACK );
        g_level_graph.pushSprite( LEVEL_X, LEVEL_Y );
        ESP_LOGI( TAG, "Level graph sprite created: %d x %d", LEVEL_W, LEVEL_H );
    }

    // OrcSDRの現在の起動方法と同じ。
    // RTL-SDR接続中にUSB railをOFF→ONしない。
    ESP_LOGI( TAG, "Enabling USB-A VBUS" );

    if ( init_sdcard() == ESP_OK ) {
        list_sdcard_root();
        load_base_screen();
        ensure_hro_config();
        draw_station_info();
        draw_waterfall_frequency_axis();
        draw_shutdown_button( false );
    }


    // Audio設定を実際のAudio制御へ反映
    g_audio_volume.store( g_hro_config.audio_volume, std::memory_order_relaxed );
    g_audio_mute.store( g_hro_config.audio_mute, std::memory_order_relaxed );
    draw_audio_controls();

    HroTuning tuning = make_hro_tuning();
    g_hro_lo_frequency_hz = tuning.actual_lo_hz;
    g_hro_freq_shift = tuning.nco_shift_hz;

    ESP_LOGI(
        TAG,
        "HRO tuning: target=%lu ideal_LO=%lu actual_LO=%lu IF=%ld NCO=%.1f Hz",
        static_cast<unsigned long>(tuning.target_rf_hz),
        static_cast<unsigned long>(tuning.ideal_lo_hz),
        static_cast<unsigned long>(tuning.actual_lo_hz),
        static_cast<long>(tuning.actual_if_hz),
        tuning.nco_shift_hz
    );


    // OrcSDRでは350ms待っている。
    // 最小試験では少し余裕を持たせ500ms。
    vTaskDelay( pdMS_TO_TICKS( 500 ) );

    // --------------------------------------------------------
    // Audio queues
    // --------------------------------------------------------
    g_audio_queue = xQueueCreate( 4, sizeof( AudioBlock ) );
    if ( g_audio_queue == nullptr ) {
        ESP_LOGE( TAG, "Failed to create audio queue" );
        return;
    }

    // --------------------------------------------------------
    // IQ ring buffer / queues
    // --------------------------------------------------------
    g_free_q = xQueueCreate( IQ_SLOT_COUNT, sizeof( uint8_t ) );
    g_filled_q = xQueueCreate( IQ_SLOT_COUNT, sizeof( RtlIqBlock ) );

    if ( g_free_q == nullptr || g_filled_q == nullptr ) {
        ESP_LOGE( TAG, "Failed to create IQ queues" );
        return;
    }

    for ( uint8_t i = 0; i < IQ_SLOT_COUNT; ++i ) {
        g_iq_slots[i] = static_cast<uint8_t *>( heap_caps_malloc( IQ_SLOT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );
        if ( g_iq_slots[i] == nullptr ) {
            ESP_LOGE( TAG, "Failed to allocate IQ slot %u", static_cast<unsigned>( i ) );
            return;
        }
        xQueueSend( g_free_q, &i, portMAX_DELAY );
    }

    ESP_LOGI( TAG,
             "IQ ring allocated: %u slots x %u bytes = %u bytes",
             static_cast<unsigned>( IQ_SLOT_COUNT ),
             static_cast<unsigned>( IQ_SLOT_BYTES ),
             static_cast<unsigned>( IQ_SLOT_COUNT * IQ_SLOT_BYTES ) );

    // ========================================
    // FFT queue 作成
    // ========================================
    g_fft_free_q = xQueueCreate( FFT_BUFFER_COUNT, sizeof( uint8_t ) );
    g_fft_ready_q = xQueueCreate( FFT_BUFFER_COUNT, sizeof( FftFrame ) );

    if ( g_fft_free_q == nullptr || g_fft_ready_q == nullptr ) {
        ESP_LOGE( TAG, "Failed to create FFT queues" );
        return;
    }

    // ========================================
    // FFT double buffer 確保
    // ========================================
    for ( uint8_t i = 0; i < FFT_BUFFER_COUNT; ++i ) {
        g_fft_i[i] = static_cast<float *>( heap_caps_malloc( FFT_SIZE * sizeof( float ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );
        g_fft_q[i] = static_cast<float *>( heap_caps_malloc( FFT_SIZE * sizeof( float ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );

        if ( g_fft_i[i] == nullptr || g_fft_q[i] == nullptr) {
            ESP_LOGE( TAG, "Failed to allocate FFT buffer %u", static_cast<unsigned>( i ) );
            return;
        }
        xQueueSend( g_fft_free_q, &i, portMAX_DELAY );
    }

    ESP_LOGI(
        TAG,
        "FFT double buffer allocated: "
        "%u x %u complex samples",
        FFT_BUFFER_COUNT,
        static_cast<unsigned>( FFT_SIZE ) );

    // ========================================
    // 最初の書き込み用FFT bufferを1個取得
    // ========================================
    if ( xQueueReceive( g_fft_free_q, &g_fft_write_slot, portMAX_DELAY) != pdTRUE ) {
        ESP_LOGE( TAG, "Failed to get first FFT buffer" );
        return;
    }
    g_fft_pos = 0;

    // ========================================
    // FFT専用の作業バッファ取得
    // ========================================
    g_fft_work = static_cast<float *>( heap_caps_malloc( FFT_SIZE * 2 * sizeof( float ), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );
    if ( g_fft_work == nullptr ) {
        ESP_LOGE( TAG, "Failed to allocate FFT work buffer" );
        return;
    }

    // ========================================
    // FFTテーブルを初期化
    // ========================================
    esp_err_t fft_ret = dsps_fft2r_init_fc32( nullptr, FFT_SIZE );
    if ( fft_ret != ESP_OK ) {
        ESP_LOGE( TAG, "FFT init failed: %s", esp_err_to_name( fft_ret ) );
        return;
    }
    ESP_LOGI( TAG, "8192-point FFT initialized" );

    const size_t history_bytes = HRO_HISTORY_SECONDS * HRO_BIN_COUNT * sizeof( float );
    g_hro_history = static_cast<float *>( heap_caps_malloc( history_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );
    if ( g_hro_history == nullptr ) {
        ESP_LOGE( TAG, "Failed to allocate HRO history buffer" );
        return;
    }
    memset( g_hro_history, 0, history_bytes );
    ESP_LOGI(
        TAG,
        "HRO history allocated: %d sec x %d bins = %u bytes",
        HRO_HISTORY_SECONDS,
        HRO_BIN_COUNT,
        static_cast<unsigned>( history_bytes ) );

    // ========================================
    // FFT task 起動
    // ========================================
    BaseType_t fft_task_ok = xTaskCreate( hro_fft_task, "hro_fft", 8192, nullptr, 4, nullptr );
    if ( fft_task_ok != pdPASS ) {
        ESP_LOGE( TAG, "Failed to create FFT task" );
        return;
    }

    // --------------------------------------------------------
    // esp_rtl_sdr
    // --------------------------------------------------------
    esp_rtl_sdr_config_t cfg;
    esp_rtl_sdr_config_default( &cfg );

    cfg.event_cb = rtl_event_callback;

    // OrcSDRで実績のある設定
    cfg.transfer_bytes = 32768;
    cfg.transfer_count = 3;

    // USB owner task
    cfg.usb_task_core_id = 0;

    // pull ringは不要
    cfg.delivery_mode = ESP_RTL_SDR_DELIVERY_CALLBACK;

    esp_err_t err = esp_rtl_sdr_config_validate( &cfg );

    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "RTL config invalid: %s", esp_rtl_sdr_err_to_name( err ) );
        return;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(
        hro_dsp_task,
        "hro_dsp",
        4096,
        nullptr,
        5,
        nullptr,
        1);

    if ( task_ok != pdPASS ) {
        ESP_LOGE( TAG, "Failed to create HRO DSP task" );
        return;
    }

    err = esp_rtl_sdr_install( &cfg, &g_rtl );

    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "RTL install failed: %s", esp_rtl_sdr_err_to_name( err ) );
        return;
    }

    ESP_LOGI( TAG, "esp_rtl_sdr installed v%s", esp_rtl_sdr_get_version_string() );

    // RTL制御/DSP側はCore 1
    BaseType_t hro_radio_task_result =
        xTaskCreatePinnedToCore(
            hro_radio_task,
            "hro_radio",
            8192,
            nullptr,
            5,
            nullptr,
            1 );	// CPU1

    if ( hro_radio_task_result != pdPASS ) {
        ESP_LOGE( TAG, "Failed to create hro_radio task" );
        return;
    }

    // スペクトルグラフ表示側はCore 0
    BaseType_t hro_display_task_result =
        xTaskCreatePinnedToCore(
            hro_display_task,
            "hro_display",
            8192,
            nullptr,
            2,
            nullptr,
            0 );	// CPU0

    if ( hro_display_task_result != pdPASS ) {
        ESP_LOGE( TAG, "Failed to create hro_display task" );
        return;
    }

    // AudioタスクはCore 0
    BaseType_t hro_audio_task_result =
        xTaskCreatePinnedToCore(
        hro_audio_task,
        "hro_audio",
        4096,
        nullptr,
        3,
        nullptr,
        0
    );

    if ( hro_audio_task_result != pdPASS ) {
        ESP_LOGE( TAG, "Failed to create hro_audio task" );
        return;
    }

    wifi_start_ap( saved_ssid, saved_password );
}

} // namespace hro::tab5::app
