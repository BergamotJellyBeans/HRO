#pragma once
#include "hro_visual_packet.h"
namespace hro::clock {
inline constexpr std::uint16_t PORT = 50003;
inline constexpr std::uint64_t MIN_MS = 1577836800000ULL;
inline constexpr std::uint64_t MAX_MS = 4102444800000ULL;
inline bool token(const char* data, std::size_t size, char* nonce) {
    if (!data || size != 23 || std::memcmp(data, "TIME 1 ", 7)) return false;
    for (unsigned i = 0; i < 16; ++i) {
        if (!hro::visual::hex(data[7+i])) return false;
        nonce[i] = hro::visual::upper(data[7+i]);
    }
    nonce[16] = 0;
    return true;
}
inline int response(char* out, std::size_t size, const char* nonce, std::uint64_t ms) {
    const int n = std::snprintf(out, size, "TIME 1 %s %llu", nonce, static_cast<unsigned long long>(ms));
    return n > 0 && static_cast<std::size_t>(n) < size ? n : -1;
}
inline bool parse(const char* data, std::size_t size, const char* expected_nonce, std::uint64_t& ms) {
    char nonce[17];
    if (!data || !expected_nonce || size < 25 || size > 37 || data[23] != ' ' || !token(data, 23, nonce) ||
        std::strcmp(nonce, expected_nonce)) return false;
    std::uint64_t value = 0;
    for (std::size_t i = 24; i < size; ++i) {
        if (data[i] < '0' || data[i] > '9') return false;
        value = value * 10 + static_cast<unsigned>(data[i] - '0');
    }
    if (value && (value < MIN_MS || value >= MAX_MS)) return false;
    ms = value;
    return true;
}
}
