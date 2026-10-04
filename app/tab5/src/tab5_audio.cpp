#include "tab5_runtime.hpp"
#include "tab5_audio.hpp"
#include "tab5_console.hpp"
#include "tab5_terminal.hpp"

namespace hro::tab5::app {
static AudioBlock g_audio_play_buffer[AUDIO_BUFFER_COUNT];
static TickType_t g_audio_config_change_tick = 0;
static std::atomic<int> g_touch_beep_samples{0};
static float g_touch_beep_phase = 0.0f;


void play_touch_beep( void )
{
    // 約40ms
    g_touch_beep_samples.store( AUDIO_SAMPLE_RATE * 40 / 1000, std::memory_order_relaxed );
}

void audio_config_changed( void )
{
    g_audio_config_dirty.store( true, std::memory_order_relaxed );
    g_audio_config_change_tick = xTaskGetTickCount();
}

void save_audio_config_if_needed( void )
{
    if ( !g_audio_config_dirty.load( std::memory_order_relaxed ) ) {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    if ( ( now - g_audio_config_change_tick ) < pdMS_TO_TICKS( 2000 ) ) {
        return;
    }

    // 現在のAudio設定をHRO設定へ反映
    stored_hro_config().display_level_db = g_display_level_db.load(std::memory_order_relaxed);
    stored_hro_config().audio_volume = g_audio_volume.load( std::memory_order_relaxed );
    stored_hro_config().audio_mute = g_audio_mute.load( std::memory_order_relaxed );

    if ( save_hro_config() ) {
        g_audio_config_dirty.store( false, std::memory_order_relaxed );
        ESP_LOGI( TAG, "Local controls saved: volume=%d mute=%d display_level=%d dB", stored_hro_config().audio_volume, stored_hro_config().audio_mute ? 1 : 0, stored_hro_config().display_level_db );
    }
}

void hro_audio_task( void *arg )
{
    ESP_LOGI( TAG, "HRO Audio task started" );

    int play_index = 0;
    uint32_t block_count = 0;
    bool playback_started = false;
    int64_t last_feed_us = 0, max_feed_gap_us = 0, last_timing_log_us = 0;
    const unsigned prebuffer_blocks = terminal_mode() ? 10 : 4;
    bool buffered = false;
    unsigned rebuffer_count = 0;

    for ( ;; ) {
        AudioBlock incoming;
        if (!buffered && uxQueueMessagesWaiting(g_audio_queue) >= prebuffer_blocks) {
            buffered = true;
            last_feed_us = 0; // Buffering delay is reported separately.
        }
        if (!buffered) {
            // Confirmation/touch sounds remain available while accumulating PCM.
            if (g_touch_beep_samples.load(std::memory_order_relaxed) <= 0) {
                // With 100 Hz ticks, pdMS_TO_TICKS(5) is zero and busy-spins.
                vTaskDelay(1);
                continue;
            }
            memset(&incoming, 0, sizeof(incoming));
        } else if ( xQueueReceive( g_audio_queue, &incoming, pdMS_TO_TICKS(20) ) != pdTRUE ) {
            buffered = false;
            ++rebuffer_count;
            console_message("Audio buffering: waiting for data", ConsoleLevel::Warning);
            if (g_touch_beep_samples.load(std::memory_order_relaxed) <= 0) continue;
            memset(&incoming, 0, sizeof(incoming));
        } else if (!playback_started) {
            playback_started = true;
            console_message("Audio playback started");
        }

        block_count++;

        // 最大絶対値
        int16_t peak = 0;
        for ( size_t i = 0; i < AUDIO_BLOCK_SAMPLES; ++i ) {
            int32_t v = incoming.samples[i];
            if ( v < 0 ) {
                v = -v;
            }
            if ( v > peak ) {
                peak = static_cast<int16_t>(v);
            }
        }

        if ( ( block_count % 100 ) == 0 ) {
            ESP_LOGI( TAG, "AUDIO: received %lu blocks peak=%d", (unsigned long)block_count, (int)peak );
        }

        AudioBlock &play = g_audio_play_buffer[play_index];
        const bool mute = g_audio_mute.load( std::memory_order_relaxed );
        const int volume = g_audio_volume.load( std::memory_order_relaxed );
        constexpr float AUDIO_GAIN = 4.0f;
        const float gain = mute ? 0.0f : AUDIO_GAIN * static_cast<float>(volume) / 100.0f;

        for ( size_t i = 0; i < AUDIO_BLOCK_SAMPLES; ++i ) {
            float s = static_cast<float>( incoming.samples[i] ) * gain;
            // Beeper
            int beep_samples = g_touch_beep_samples.load( std::memory_order_relaxed );
            if ( beep_samples > 0 ) {
                // 1200 Hz の短いタッチ音
                constexpr float BEEP_FREQ = 1200.0f;
                constexpr float BEEP_LEVEL = 3000.0f;
                s += sinf(g_touch_beep_phase) * BEEP_LEVEL;
                g_touch_beep_phase += 2.0f * M_PI * BEEP_FREQ / static_cast<float>( AUDIO_SAMPLE_RATE );
                if ( g_touch_beep_phase >= 2.0f * M_PI ) {
                    g_touch_beep_phase -= 2.0f * M_PI;
                }
                g_touch_beep_samples.store( beep_samples - 1, std::memory_order_relaxed );
            }
            if ( s > 32767.0f ) {
                s = 32767.0f;
            } else if ( s < -32768.0f ) {
                s = -32768.0f;
            }
            play.samples[i] = static_cast<int16_t>( s );
        }
        bool ok = M5.Speaker.playRaw(
            play.samples,
            AUDIO_BLOCK_SAMPLES,
            AUDIO_SAMPLE_RATE,
            false,      // mono
            1,          // repeat
            0,          // channel 0
            false       // 現在音を強制停止しない
        );

        if ( !ok ) {
            ESP_LOGW( TAG, "Speaker playRaw queue full" );
        }
        const int64_t feed_us = esp_timer_get_time();
        if (last_feed_us && feed_us - last_feed_us > max_feed_gap_us)
            max_feed_gap_us = feed_us - last_feed_us;
        last_feed_us = feed_us;
        if (feed_us - last_timing_log_us >= 10000000) {
            ESP_LOGI(TAG, "AUDIO timing: max_feed_gap=%lld us queue=%u rebuffers=%u",
                     static_cast<long long>(max_feed_gap_us),
                     static_cast<unsigned>(uxQueueMessagesWaiting(g_audio_queue)), rebuffer_count);
            max_feed_gap_us = 0;
            last_timing_log_us = feed_us;
        }

        play_index++;

        if ( play_index >= AUDIO_BUFFER_COUNT ) {
            play_index = 0;
        }
        // Even a full queue / immediate Speaker acceptance must leave idle time.
        vTaskDelay(1);
    }
}

} // namespace hro::tab5::app
