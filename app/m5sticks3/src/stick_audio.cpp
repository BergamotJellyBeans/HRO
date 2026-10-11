#include "stick_audio.hpp"
#include "stick_led.hpp"
#include "M5GFX.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cmath>
#include <cstring>
#include <atomic>

namespace stick {
namespace {
i2s_chan_handle_t tx = nullptr;
TaskHandle_t player = nullptr;
QueueHandle_t tones = nullptr;
std::atomic<bool> healthy{false};
std::atomic<bool> playing{false};
constexpr int bus = 1, pmic = 0x6e;
bool amp(bool enabled) {
    return (enabled ? lgfx::i2c::bitOn(bus, pmic, 0x11, 8, 100000)
                    : lgfx::i2c::bitOff(bus, pmic, 0x11, 8, 100000)).has_value();
}
bool configure_codec() {
    // Match M5Unified 0.2.20 _speaker_enabled_cb_sticks3: apply on
    // every speaker enable, not only at boot. Preserve its 0xBF DAC gain.
    constexpr uint8_t registers[][2] = {{0x00,0x80},{0x01,0xB5},{0x02,0x18},{0x0D,0x01},
        {0x12,0x00},{0x13,0x10},{0x32,0xBF},{0x37,0x08}};
    for (const auto& reg : registers) {
        bool written = false;
        for (unsigned attempt = 0; attempt < 3 && !written; ++attempt) {
            written = lgfx::i2c::writeRegister8(bus, 0x18, reg[0], reg[1], 0, 100000).has_value();
        }
        if (!written) {
            ESP_LOGW("stick_audio", "Codec setup failed at register 0x%02X", reg[0]);
            return false;
        }
    }
    return true;
}
struct Tone { unsigned count; bool connection; };
void play_pattern(const Tone& tone) {
    const unsigned frequency = tone.connection ? 2000 : 1000;
    const unsigned duration_ms = tone.connection ? 100 : 50;
    const unsigned gap_ms = tone.connection ? 100 : 50;
    const unsigned frames = 22050 * duration_ms / 1000;
    const unsigned gap = 22050 * gap_ms / 1000;
    const unsigned period = frames + gap;
    const std::uint64_t total = static_cast<std::uint64_t>(tone.count) * period - gap;
    int16_t samples[220 * 2];
    const esp_err_t start = i2s_channel_enable(tx);
    const bool amplifier = start == ESP_OK && amp(true);
    const bool codec = amplifier && configure_codec();
    bool ok = start == ESP_OK && amplifier && codec;
    ESP_LOGI("stick_audio", "Tone count=%u duration=%ums Hz=%u I2S=%s amp=%s codec=%s", tone.count,
        duration_ms, frequency, esp_err_to_name(start), amplifier ? "OK" : "failed", codec ? "OK" : "failed");
    // Supply continuous silent PCM while codec/PA clocks settle, rather than
    // leaving an enabled DMA channel empty before a short notification.
    std::memset(samples, 0, sizeof(samples));
    const unsigned warmup_chunks = tone.connection ? 104 : 0; // ~1s silent clocking stabilizes connection playback after side-button reset.
    for (unsigned i = 0; ok && i < warmup_chunks; ++i) {
        size_t written = 0;
        const esp_err_t result = i2s_channel_write(tx, samples, sizeof(samples), &written, 100);
        ok = result == ESP_OK && written == sizeof(samples);
        if (!ok) ESP_LOGW("stick_audio", "Warm-up write failed: %s bytes=%u",
            esp_err_to_name(result), static_cast<unsigned>(written));
    }
    ESP_LOGI("stick_audio", "Warm-up complete; connection=%u", tone.connection);
    unsigned logged_tones = 0;
    for (std::uint64_t position = 0; ok && position < total; position += 220) {
        const unsigned tone_index = static_cast<unsigned>(position / period);
        if (tone_index >= logged_tones) {
            logged_tones = tone_index + 1;
            ESP_LOGI("stick_audio", "Submitting tone %u/%u", logged_tones, tone.count);
        }
        const unsigned chunk = static_cast<unsigned>(std::min<std::uint64_t>(220, total - position));
        for (unsigned i = 0; i < chunk; ++i) {
            const unsigned n = (position + i) % period;
            int16_t sample = 0;
            if (n < frames) {
                const float fade = std::fmin(1.0f, std::fmin(n / 55.0f, (frames - 1 - n) / 55.0f));
                sample = static_cast<int16_t>(4000 * fade * std::sin(6.2831853f * frequency * n / 22050));
            }
            samples[2*i] = samples[2*i+1] = sample;
        }
        size_t written = 0;
        const auto size = chunk * 2 * sizeof(int16_t);
        ok = i2s_channel_write(tx, samples, size, &written, 100) == ESP_OK && written == size;
    }
    if (ok) {
        std::memset(samples, 0, sizeof(samples));
        size_t written = 0;
        ok = i2s_channel_write(tx, samples, sizeof(samples), &written, 100) == ESP_OK && written == sizeof(samples);
        // Three 220-frame DMA buffers hold about 30ms at 22050Hz.
        // Allow the complete tail to leave DMA before muting the amplifier.
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    const bool muted = amp(false);
    const esp_err_t stopped = i2s_channel_disable(tx);
    ok = ok && muted && stopped == ESP_OK;
    healthy.store(ok);
    ESP_LOGI("stick_audio", "Tone complete: playback=%s mute=%s stop=%s",
        ok ? "OK" : "failed", muted ? "OK" : "failed", esp_err_to_name(stopped));
    if (!ok) ESP_LOGW("stick_audio", "Notification pattern playback failed");
    vTaskDelay(pdMS_TO_TICKS(40));
}
void play_task(void*) {
    for (;;) {
        Tone tone{};
        if (xQueueReceive(tones, &tone, portMAX_DELAY) == pdTRUE) {
            playing.store(true);
            play_pattern(tone);
            playing.store(false);
        }
    }
}
void enqueue(unsigned count, bool connection) {
    if (!count) return;
    led_notify(connection ? 2 : 1);
    const Tone tone{count, connection};
    if (tones && xQueueSend(tones, &tone, 0) != pdTRUE)
        ESP_LOGW("stick_audio", "Notification queue full");
}
}
bool audio_begin() {
    // Verified against official M5Unified 0.2.20 StickS3 setup and callback:
    // https://github.com/m5stack/M5Unified/blob/0.2.20/src/M5Unified.cpp
    if (!lgfx::i2c::init(bus, 47, 48).has_value() ||
        !lgfx::i2c::bitOff(bus, pmic, 0x16, 8, 100000).has_value() ||
        !lgfx::i2c::bitOn(bus, pmic, 0x10, 8, 100000).has_value() ||
        !lgfx::i2c::bitOff(bus, pmic, 0x13, 8, 100000).has_value() || !amp(false)) return false;
    if (!configure_codec()) return false;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 3; channel.dma_frame_num = 220;
    channel.auto_clear = true;
    if (i2s_new_channel(&channel, &tx, nullptr) != ESP_OK) return false;
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(22050);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    // Match official Speaker_Class ESP32-S3 slot alignment.
    config.slot_cfg.left_align = true;
    config.gpio_cfg.mclk = GPIO_NUM_18; config.gpio_cfg.bclk = GPIO_NUM_17;
    config.gpio_cfg.ws = GPIO_NUM_15; config.gpio_cfg.dout = GPIO_NUM_14;
    config.gpio_cfg.din = I2S_GPIO_UNUSED;
    tones = xQueueCreate(16, sizeof(Tone));
    if (!tones || i2s_channel_init_std_mode(tx, &config) != ESP_OK ||
        xTaskCreate(play_task, "stick_ack_audio", 6144, nullptr, 2, &player) != pdPASS) {
        i2s_del_channel(tx); tx = nullptr;
        if (tones) { vQueueDelete(tones); tones = nullptr; }
        return false;
    }
    healthy.store(true);
    ESP_LOGI("stick_audio", "ACK speaker ready");
    return true;
}
void audio_ack(unsigned count) { enqueue(count, false); }
void audio_connected() {
    // Keep both connection tones together, even if an ACK arrives meanwhile.
    ESP_LOGI("stick_audio", "Connection notification requested; speaker=%s",
        tones ? "ready" : "unavailable");
    enqueue(2, true);
}
bool audio_ready() { return healthy.load(); }
bool audio_busy() { return playing.load() || audio_queue_size() != 0; }
unsigned audio_stack_free() { return player ? static_cast<unsigned>(uxTaskGetStackHighWaterMark(player)) : 0; }
unsigned audio_queue_size() { return tones ? static_cast<unsigned>(uxQueueMessagesWaiting(tones)) : 0; }
}
