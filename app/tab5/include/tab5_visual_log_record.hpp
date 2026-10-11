#pragma once
#include "hro_visual_packet.h"
#include "hro_plot.h"
#include <ctime>
namespace hro::tab5::app {
struct VisualLogRecord {
    hro::visual::Request request;
    std::uint64_t utc_ms = 0;
    std::int64_t uptime_us = 0;
};
inline time_t visual_csv_block(const VisualLogRecord& record) {
    return static_cast<time_t>(hro::plot::blockStart(static_cast<std::int64_t>(record.utc_ms / 1000)));
}
inline bool visual_csv_filename(const char* png, char* out, std::size_t capacity) {
    if (!png || !out) return false;
    const auto n = std::strlen(png);
    if (n < 4 || std::strcmp(png + n - 4, ".png") || capacity <= n) return false;
    std::memcpy(out, png, n + 1);
    std::memcpy(out + n - 3, "csv", 3);
    return true;
}
inline constexpr const char* VISUAL_CSV_HEADER = "received_jst,received_unix_ms,tab5_uptime_ms,stick_id,event_id,stick_count,meteor_count,block_second\n";
inline bool visual_csv_time_fields(std::uint64_t ms, char* jst, std::size_t jst_size,
                                   char* block_second, std::size_t block_size) {
    std::snprintf(jst, jst_size, "UNSYNC");
    if (block_size) block_second[0] = 0;
    if (!ms) return true;
    const time_t seconds = static_cast<time_t>(ms / 1000 + 9 * 3600);
    tm date{};
    if (!gmtime_r(&seconds, &date) || !std::strftime(jst, jst_size, "%Y-%m-%d %H:%M:%S", &date)) return false;
    const auto length = std::strlen(jst);
    const int n = std::snprintf(jst + length, jst_size - length, ".%03u", static_cast<unsigned>(ms % 1000));
    const int b = std::snprintf(block_second, block_size, "%u", static_cast<unsigned>((ms / 1000) % hro::plot::SECONDS));
    return n > 0 && static_cast<std::size_t>(n) < jst_size - length &&
        b > 0 && static_cast<std::size_t>(b) < block_size;
}
inline int visual_csv_row(char* out, std::size_t capacity, const VisualLogRecord& record) {
    char jst[32], block_second[8];
    if (!visual_csv_time_fields(record.utc_ms, jst, sizeof(jst), block_second, sizeof(block_second))) return -1;
    char count[16] = {};
    if (record.request.version >= 2) std::snprintf(count, sizeof(count), "%u", static_cast<unsigned>(record.request.sequence));
    const int n = std::snprintf(out, capacity, "%s,%llu,%lld,%s,%s,%s,%u,%s\n", jst,
        static_cast<unsigned long long>(record.utc_ms), static_cast<long long>(record.uptime_us / 1000),
        record.request.stick_id, record.request.event_id, count, static_cast<unsigned>(record.request.meteor_count), block_second);
    return n > 0 && static_cast<std::size_t>(n) < capacity ? n : -1;
}
}
