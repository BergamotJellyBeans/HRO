#include "tab5_runtime.hpp"
#include "tab5_dsp.hpp"

namespace hro::tab5::app {
static std::atomic<uint32_t> g_decim_power{0};
static ComplexSample g_decim_buffer[DECIM_BUFFER_SAMPLES];
static size_t g_decim_buffer_pos = 0;
static hro::dsp::Resampler16_125 g_resampler;
static std::atomic<uint32_t>g_resamp_power{0};
static AudioBlock g_audio_build_block;
static size_t g_audio_build_pos = 0;
static std::atomic<uint32_t> g_audio_drops{0};
static void init_resampler();
static bool process_resampler_input(float i, float q, float& oi, float& oq);

static void init_resampler()
{
    g_resampler.reset();
    ESP_LOGI(TAG, "Shared resampler: 64000 -> 8192 sps, 16/125");
}

static bool process_resampler_input(float i, float q, float& oi, float& oq)
{
    std::complex<float> output;
    if (!g_resampler.processOne({i, q}, output)) return false;
    oi = output.real(); oq = output.imag();
    return true;
}

void hro_dsp_task( void * )
{
    RtlIqBlock block{};

    hro::tab5::Frontend frontend;
    hro::dsp::FloatNcoShifter nco(hro::tab5::OUTPUT_RATE);
    nco.setFrequencyShift(g_hro_freq_shift);
    float power_sum = 0.0f;
    uint32_t power_count = 0;

    ESP_LOGI( TAG, "HRO DSP task started: shift=%.1f Hz", g_hro_freq_shift );

    float test_i = 1.0f;
    float test_q = 0.0f;
    const float test_c = cosf( TWO_PI * HRO_TEST_TONE_HZ / HRO_SAMPLE_RATE );
    const float test_s = sinf( TWO_PI * HRO_TEST_TONE_HZ / HRO_SAMPLE_RATE );

    init_resampler();

    TickType_t last_idle_yield = xTaskGetTickCount();

    while ( true ) {
        if ( xQueueReceive( g_filled_q, &block,portMAX_DELAY ) != pdTRUE ) {
            continue;
        }

        // CU8なので I,Q が1 byteずつ交互
        const size_t samples = block.bytes / 2;

        uint32_t decim_count = 0, resamp_count = 0;
        float i, q;
        for ( size_t n = 0; n < samples; ++n ) {
            if ( HRO_TEST_TONE ) {
                // +1000 Hz complex test tone
                i = test_i;
                q = test_q;
                const float new_i = test_i * test_c - test_q * test_s;
                const float new_q = test_i * test_s + test_q * test_c;
                test_i = new_i;
                test_q = new_q;
            } else {
                // Real RTL-SDR IQ
                i = hro::dsp::normalizeCu8(block.data[n * 2]);
                q = hro::dsp::normalizeCu8(block.data[n * 2 + 1]);

            }
            std::complex<float> shifted;
            if (frontend.process(i, q, shifted, !HRO_TEST_TONE)) {
                const float out_i = shifted.real(), out_q = shifted.imag();
                nco.process(&shifted, 1);
                float resamp_i = 0.0f;
                float resamp_q = 0.0f;
                if (process_resampler_input(shifted.real(), shifted.imag(), resamp_i, resamp_q)) {
                    // -------------------------
                    // Audio sample
                    // CPU1ではPCM化だけ。Gain/VolumeはAudio Task(CPU0)で処理する
                    // -------------------------
                    float s = resamp_i * 32767.0f;
                    if ( s > 32767.0f ) {
                        s = 32767.0f;
                    } else if ( s < -32768.0f ) {
                        s = -32768.0f;
                    }

                    g_audio_build_block.samples[g_audio_build_pos++] = static_cast<int16_t>( s );
                    if ( g_audio_build_pos >= AUDIO_BLOCK_SAMPLES ) {
                        if ( g_audio_queue != nullptr ) {
                            if ( xQueueSend( g_audio_queue, &g_audio_build_block, 0 ) != pdTRUE ) {
                                g_audio_drops.fetch_add( 1, std::memory_order_relaxed );
                            }
                        }
                        g_audio_build_pos = 0;
                    }
                    ++resamp_count;
                    const float p = resamp_i * resamp_i + resamp_q * resamp_q;
                    // Publish diagnostic counts once per input block.
                    g_resamp_power.store( static_cast<uint32_t>( p * 1000000.0f ), std::memory_order_relaxed );

                    // 8192 samples = 1 FFT frame
                    g_fft_i[g_fft_write_slot][g_fft_pos] = resamp_i;
                    g_fft_q[g_fft_write_slot][g_fft_pos] = resamp_q;
                    g_fft_pos++;
                    if ( g_fft_pos >= FFT_SIZE ) {
                        FftFrame frame{
                            g_fft_i[g_fft_write_slot],
                            g_fft_q[g_fft_write_slot],
                            g_fft_write_slot
                        };

                        if ( xQueueSend( g_fft_ready_q, &frame, 0 ) == pdTRUE ) {
                            g_fft_frames.fetch_add( 1, std::memory_order_relaxed );
                        } else {
                            // 通常ここには来ない
                            g_fft_drops.fetch_add( 1, std::memory_order_relaxed );
                            g_fft_pos = 0;
                            return;
                        }

                        // 次の空きFFTバッファを取得
                        uint8_t next_slot;

                        if ( xQueueReceive( g_fft_free_q, &next_slot, 0 ) == pdTRUE ) {
                            g_fft_write_slot = next_slot;
                            g_fft_pos = 0;
                        } else {
                            // FFT側がまだ前のバッファを処理中
                            g_fft_drops.fetch_add( 1, std::memory_order_relaxed );
                            g_fft_pos = 0;
                        }
                    }
                }

                g_decim_buffer[g_decim_buffer_pos].i = out_i;
                g_decim_buffer[g_decim_buffer_pos].q = out_q;
                g_decim_buffer_pos++;
                if ( g_decim_buffer_pos >= DECIM_BUFFER_SAMPLES ) {
                    g_decim_buffer_pos = 0;
                }

                // 今はまだ次段へ渡さない
                // 64 kS/s IQがここにできている
                const float power =	out_i * out_i +	out_q * out_q;

                power_sum += power;
                power_count++;

                ++decim_count;
            }
        }
        TickType_t now = xTaskGetTickCount();
        if ( now - last_idle_yield >= pdMS_TO_TICKS( 2000 ) ) {
            last_idle_yield = now;
//    		vTaskDelay( 1 );
        }

        if ( power_count > 0 ) {
            const float avg_power =	static_cast<float>( power_sum / power_count );
            g_decim_power.store( static_cast<uint32_t>( avg_power * 1000000.0f ), std::memory_order_relaxed );
            power_sum = 0.0f;
            power_count = 0;
        }

        g_decim_interval_samples.fetch_add(decim_count, std::memory_order_relaxed);
        g_resamp_interval_samples.fetch_add(resamp_count, std::memory_order_relaxed);
        g_resamp_10sec_samples.fetch_add(resamp_count, std::memory_order_relaxed);
        g_dsp_interval_bytes.fetch_add( static_cast<uint32_t>( block.bytes ), std::memory_order_relaxed );
        g_dsp_interval_blocks.fetch_add( 1, std::memory_order_relaxed );

        (void)xQueueSend( g_free_q, &block.slot, portMAX_DELAY );
    }
}

} // namespace hro::tab5::app
