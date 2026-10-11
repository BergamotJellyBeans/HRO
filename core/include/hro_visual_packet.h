#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hro::visual {
inline constexpr std::uint16_t PORT = 50003;
inline constexpr std::size_t REQUEST_SIZE = 32;
inline constexpr std::size_t COUNTED_REQUEST_SIZE = 43;
struct Request { char stick_id[7] = {}; char event_id[17] = {}; unsigned version = 1; std::uint32_t sequence = 0; std::uint32_t meteor_count = 1; };
inline bool hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
inline char upper(char c) { return (c >= 'a' && c <= 'f') ? c - 'a' + 'A' : c; }
inline bool decimal(const char* data, std::size_t size, std::uint32_t& out) {
    if (!size || size > 10) return false;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] < '0' || data[i] > '9') return false;
        value = value * 10 + (data[i] - '0');
    }
    if (!value || value > UINT32_MAX) return false;
    out = static_cast<std::uint32_t>(value); return true;
}
// Version 3 adds a positive meteor count before the 10-digit event sequence.
inline bool parse(const char* data, std::size_t size, Request& out) {
    if (!data || size < REQUEST_SIZE || size > 54 || std::memcmp(data, "VISUAL ", 7) ||
        data[8] != ' ' || data[15] != ' ') return false;
    const unsigned version = data[7] - '0';
    if ((version == 1 && size != 32) || (version == 2 && size != 43) ||
        (version == 3 && size < 45) || version < 1 || version > 3) return false;
    Request parsed; parsed.version = version;
    for (unsigned i = 0; i < 6; ++i) {
        if (!hex(data[9 + i])) return false;
        parsed.stick_id[i] = upper(data[9 + i]);
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (!hex(data[16 + i])) return false;
        parsed.event_id[i] = upper(data[16 + i]);
    }
    if (version >= 2) {
        if (data[32] != ' ' || !decimal(data + size - 10, 10, parsed.sequence)) return false;
        if (version == 3 && (data[size - 11] != ' ' ||
            !decimal(data + 33, size - 44, parsed.meteor_count))) return false;
    }
    out = parsed; return true;
}
inline int encode(char* out, std::size_t capacity, const Request& request) {
    int n;
    if (request.version == 3) n = std::snprintf(out, capacity, "VISUAL 3 %s %s %u %010u", request.stick_id,
        request.event_id, static_cast<unsigned>(request.meteor_count), static_cast<unsigned>(request.sequence));
    else if (request.version == 2) n = std::snprintf(out, capacity, "VISUAL 2 %s %s %010u", request.stick_id,
        request.event_id, static_cast<unsigned>(request.sequence));
    else n = std::snprintf(out, capacity, "VISUAL 1 %s %s", request.stick_id, request.event_id);
    return n > 0 && static_cast<std::size_t>(n) < capacity ? n : -1;
}
inline int reply_prefix(char* out, std::size_t capacity, const char* kind, const Request& request) {
    if (request.version == 3) return std::snprintf(out, capacity, "%s 3 %s %s %u %010u ", kind,
        request.stick_id, request.event_id, static_cast<unsigned>(request.meteor_count), static_cast<unsigned>(request.sequence));
    return request.version == 2
        ? std::snprintf(out, capacity, "%s 2 %s %s %010u ", kind, request.stick_id, request.event_id, static_cast<unsigned>(request.sequence))
        : std::snprintf(out, capacity, "%s 1 %s %s ", kind, request.stick_id, request.event_id);
}
inline int ack(char* out, std::size_t capacity, const Request& request, std::uint64_t received_ms) {
    const int prefix = reply_prefix(out, capacity, "ACK", request);
    if (prefix < 0 || static_cast<std::size_t>(prefix) >= capacity) return -1;
    const int n = std::snprintf(out + prefix, capacity - prefix, "%llu", static_cast<unsigned long long>(received_ms));
    return n > 0 && static_cast<std::size_t>(prefix + n) < capacity ? prefix + n : -1;
}
inline int busy_reply(char* out, std::size_t capacity, const Request& request) {
    const int prefix = reply_prefix(out, capacity, "NACK", request);
    if (prefix < 0 || static_cast<std::size_t>(prefix) >= capacity) return -1;
    const int n = std::snprintf(out + prefix, capacity - prefix, "BUSY");
    return n > 0 && static_cast<std::size_t>(prefix + n) < capacity ? prefix + n : -1;
}
inline bool matches_ack(const char* data, std::size_t size, const Request& request) {
    char prefix[64];
    const int length = reply_prefix(prefix, sizeof(prefix), "ACK", request);
    if (!data || length <= 0 || size <= static_cast<std::size_t>(length) ||
        size > static_cast<std::size_t>(length + 20) || std::memcmp(data, prefix, length)) return false;
    std::uint64_t value = 0;
    for (std::size_t i = length; i < size; ++i) {
        if (data[i] < '0' || data[i] > '9') return false;
        const unsigned digit = data[i] - '0';
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}
inline bool matches_busy(const char* data, std::size_t size, const Request& request) {
    char expected[64];
    const int length = busy_reply(expected, sizeof(expected), request);
    return data && length > 0 && size == static_cast<std::size_t>(length) && !std::memcmp(data, expected, size);
}
}
