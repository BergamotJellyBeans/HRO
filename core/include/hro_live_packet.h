#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace hro::live {
inline constexpr std::size_t BIN_COUNT = 601;
inline constexpr std::size_t FFT_PACKET_BYTES = 28 + BIN_COUNT * 4;
inline constexpr std::size_t AUDIO_SAMPLES = 256;
inline constexpr std::size_t AUDIO_PACKET_BYTES = AUDIO_SAMPLES * 4;
struct Frame {
    uint64_t sequence;
    int64_t timestamp_ms;
    float peak_db;
    float fft_db[BIN_COUNT];
};
inline uint32_t read32(const uint8_t* p)
{ return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline uint64_t read64(const uint8_t* p)
{ return uint64_t(read32(p)) << 32 | read32(p + 4); }
inline float asFloat(uint32_t bits)
{ float value; static_assert(sizeof(value) == 4); std::memcpy(&value, &bits, 4); return value; }
inline bool decodeFrame(const uint8_t* data, std::size_t size, Frame& frame)
{
    if (!data || size != FFT_PACKET_BYTES || read32(data) != 0x48524f31 ||
        data[4] != 0 || data[5] != 1 || (unsigned(data[6]) << 8 | data[7]) != BIN_COUNT)
        return false;
    frame.sequence = read64(data + 8);
    const auto timestamp = read64(data + 16);
    // Reject corrupt timestamps before any calendar/time conversions.
    if (timestamp < 946684800000ULL || timestamp > 4102444800000ULL) return false;
    frame.timestamp_ms = static_cast<int64_t>(timestamp);
    frame.peak_db = asFloat(read32(data + 24));
    if (!std::isfinite(frame.peak_db)) return false;
    for (std::size_t i = 0; i < BIN_COUNT; ++i) {
        frame.fft_db[i] = asFloat(read32(data + 28 + i * 4));
        if (!std::isfinite(frame.fft_db[i])) return false;
    }
    return true;
}
inline bool decodeAudio(const uint8_t* data, std::size_t size, int16_t* output)
{
    if (!data || !output || size != AUDIO_PACKET_BYTES) return false;
    for (std::size_t i = 0; i < AUDIO_SAMPLES; ++i) {
        const auto* p = data + i * 4;
        const uint32_t bits = uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
        float sample = asFloat(bits);
        if (!std::isfinite(sample)) return false;
        sample *= 32767.0f;
        output[i] = static_cast<int16_t>(sample > 32767 ? 32767 : sample < -32768 ? -32768 : sample);
    }
    return true;
}
}
