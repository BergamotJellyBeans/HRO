#include "tab5_runtime.hpp"
#include "tab5_fft.hpp"

namespace hro::tab5::app {
static std::atomic<uint32_t> g_hro_history_sequence{0};


void hro_fft_task( void * )
{
    FftFrame frame{};

    ESP_LOGI( TAG, "HRO FFT task started" );

    while ( true ) {
        if ( xQueueReceive( g_fft_ready_q, &frame, portMAX_DELAY) != pdTRUE ) {
            continue;
        }

        // ----------------------------------------
        // IQ -> interleaved complex FFT buffer
        // + Hann window
        // ----------------------------------------

        for ( int n = 0; n < FFT_SIZE; ++n ) {
            const float w = hro::dsp::HannWindow::coefficient(n, FFT_SIZE);
            g_fft_work[n * 2 + 0] = frame.i[n] * w;
            g_fft_work[n * 2 + 1] = frame.q[n] * w;
        }

        // ----------------------------------------
        // 8192-point complex FFT
        // ----------------------------------------
        esp_err_t ret = dsps_fft2r_fc32( g_fft_work, FFT_SIZE );
        if ( ret == ESP_OK ) {
            ret = dsps_bit_rev_fc32( g_fft_work, FFT_SIZE );
        }

        // ----------------------------------------
        // Search 480 ... 1080 Hz
        // ----------------------------------------
        if ( ret == ESP_OK ) {
            float max_power = 0.0f;
            int max_bin = 0;
            const int first_bin = hro_fft_min_hz();
            for ( int bin = first_bin; bin <= hro_fft_max_hz(); ++bin ) {
                const float re = g_fft_work[bin * 2 + 0];
                const float im = g_fft_work[bin * 2 + 1];
                const float power = re * re + im * im;

                // 480 Hz -> index 0
                // 481 Hz -> index 1
                // ...
                // 1080 Hz -> index 600
                g_hro_spectrum[bin - first_bin] = hro::dsp::powerDb(power);
                if (power > max_power) {
                    max_power = power;
                    max_bin = bin;
                }
            }

            // 601 bins全部を書き終わってから更新
            g_hro_spectrum_sequence.fetch_add( 1, std::memory_order_release );

            if ( g_hro_history != nullptr ) {
                float *dst = &g_hro_history[static_cast<size_t>( g_hro_history_write_pos ) * HRO_BIN_COUNT];
                memcpy( dst, g_hro_spectrum, HRO_BIN_COUNT * sizeof( float ) );

                // 次の書き込み位置
                g_hro_history_write_pos++;

                if ( g_hro_history_write_pos >= HRO_HISTORY_SECONDS ) {
                    g_hro_history_write_pos = 0;
                }

                // 起動後1200秒までは蓄積数を増やす
                if ( g_hro_history_count < HRO_HISTORY_SECONDS ) {
                    g_hro_history_count++;
                }
                g_hro_history_sequence.fetch_add( 1, std::memory_order_release );
            }
            const float power_db = 10.0f * log10f( max_power + 1.0e-20f );
            ESP_LOGI( TAG, "FFT PEAK: %d Hz  %.1f dB", max_bin, power_db );
        } else {
            ESP_LOGE( TAG, "FFT failed: %s", esp_err_to_name( ret ) );
        }

        // ----------------------------------------
        // Frame processed
        // ----------------------------------------
        g_fft_processed.fetch_add( 1, std::memory_order_relaxed );

        // ----------------------------------------
        // Return buffer to DSP
        // ----------------------------------------
        (void)xQueueSend( g_fft_free_q, &frame.slot, portMAX_DELAY );

        // 10秒ログ
        if ( ( g_hro_history_sequence.load() % 10 ) == 0 ) {
            ESP_LOGI(
            TAG,
            "HISTORY: count=%u write_pos=%u seq=%lu",
            static_cast<unsigned>( g_hro_history_count ),
            static_cast<unsigned>( g_hro_history_write_pos ),
            static_cast<unsigned long>(
            g_hro_history_sequence.load() ) );
        }
    }
}

} // namespace hro::tab5::app
