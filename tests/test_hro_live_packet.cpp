#include "hro_live_packet.h"
#include <array>
#include <cassert>
#include <limits>

namespace {
void put32(uint8_t* p, uint32_t v)
{ for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (24 - i * 8)); }
void put64(uint8_t* p, uint64_t v)
{ put32(p, static_cast<uint32_t>(v >> 32)); put32(p + 4, static_cast<uint32_t>(v)); }
void putFloat(uint8_t* p, float v)
{ uint32_t bits; std::memcpy(&bits, &v, 4); put32(p, bits); }
}
int main()
{
    std::array<uint8_t, hro::live::FFT_PACKET_BYTES> packet{};
    put32(packet.data(), 0x48524f31);
    packet[5] = 1; packet[6] = 2; packet[7] = 89;
    put64(packet.data() + 8, 0x100000002ULL);
    put64(packet.data() + 16, 1791030000123ULL);
    putFloat(packet.data() + 24, 15.5f);
    for (unsigned i = 0; i < 601; ++i) putFloat(packet.data() + 28 + i * 4, -static_cast<float>(i));
    hro::live::Frame frame{};
    assert(hro::live::decodeFrame(packet.data(), packet.size(), frame));
    assert(frame.sequence == 0x100000002ULL && frame.timestamp_ms == 1791030000123LL);
    assert(frame.peak_db == 15.5f && frame.fft_db[600] == -600.0f);
    assert(!hro::live::decodeFrame(packet.data(), packet.size() - 1, frame));
    packet[5] = 2;
    assert(!hro::live::decodeFrame(packet.data(), packet.size(), frame));
    packet[5] = 1;
    putFloat(packet.data() + 28, std::numeric_limits<float>::quiet_NaN());
    assert(!hro::live::decodeFrame(packet.data(), packet.size(), frame));

    std::array<uint8_t, hro::live::AUDIO_PACKET_BYTES> audio{};
    const uint8_t samples[] = {0,0,0,0x3f, 0,0,0,0xbf, 0,0,0,0x40, 0,0,0,0xc0};
    std::memcpy(audio.data(), samples, sizeof(samples));
    std::array<int16_t,256> output{};
    assert(hro::live::decodeAudio(audio.data(), audio.size(), output.data()));
    assert(output[0] == 16383 && output[1] == -16383 && output[2] == 32767 && output[3] == -32768);
    assert(!hro::live::decodeAudio(audio.data(), audio.size() - 1, output.data()));
    audio[3] = 0x7f; audio[2] = 0x80; // +infinity in little-endian float32.
    assert(!hro::live::decodeAudio(audio.data(), audio.size(), output.data()));
}
