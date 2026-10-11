#pragma once
#include "tab5_visual_log_record.hpp"
#include <cerrno>
#include <unistd.h>
namespace hro::tab5::app {
// Upgrade old UTC six/seven-column files without mixing CSV schemas.
inline bool visual_csv_upgrade(const char* path) {
    constexpr const char* old_header6 = "received_utc,received_unix_ms,tab5_uptime_ms,stick_id,event_id,stick_count\n";
    constexpr const char* old_header7 = "received_utc,received_unix_ms,tab5_uptime_ms,stick_id,event_id,stick_count,meteor_count\n";
    FILE* source = std::fopen(path, "r");
    if (!source) return errno == ENOENT;
    char line[256];
    if (!std::fgets(line, sizeof(line), source)) {
        const bool empty = std::feof(source); std::fclose(source); return empty;
    }
    if (!std::strcmp(line, VISUAL_CSV_HEADER)) { std::fclose(source); return true; }
    const bool six_columns = !std::strcmp(line, old_header6);
    if (!six_columns && std::strcmp(line, old_header7)) { std::fclose(source); return false; }
    char temporary[192], backup[192];
    const int t = std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    const int b = std::snprintf(backup, sizeof(backup), "%s.utc%u", path, six_columns ? 6 : 7);
    if (t <= 0 || b <= 0 || t >= static_cast<int>(sizeof(temporary)) || b >= static_cast<int>(sizeof(backup)) ||
        access(backup, F_OK) == 0) { std::fclose(source); return false; }
    FILE* target = std::fopen(temporary, "w");
    if (!target) { std::fclose(source); return false; }
    bool ok = std::fputs(VISUAL_CSV_HEADER, target) >= 0;
    while (ok && std::fgets(line, sizeof(line), source)) {
        const auto n = std::strlen(line);
        if (!n || line[n-1] != '\n') { ok = false; break; }
        line[n-1] = 0;
        unsigned commas = 0;
        for (const char* c = line; *c; ++c) if (*c == ',') ++commas;
        char* first = std::strchr(line, ',');
        char* second = first ? std::strchr(first + 1, ',') : nullptr;
        if (!second || commas != (six_columns ? 5u : 6u) || second == first + 1) { ok = false; break; }
        std::uint64_t ms = 0;
        for (const char* c = first + 1; c < second; ++c) {
            if (*c < '0' || *c > '9' || ms > (UINT64_MAX - (*c - '0')) / 10) { ok = false; break; }
            ms = ms * 10 + (*c - '0');
        }
        if (!ok) break;
        char jst[32], block_second[8];
        if (!visual_csv_time_fields(ms, jst, sizeof(jst), block_second, sizeof(block_second))) { ok = false; break; }
        ok = std::fprintf(target, "%s,%s%s,%s\n", jst, first + 1,
            six_columns ? ",1" : "", block_second) > 0;
    }
    if (std::ferror(source)) ok = false;
    if (std::fclose(source) != 0) ok = false;
    if (ok) ok = std::fflush(target) == 0 && fsync(fileno(target)) == 0;
    if (std::fclose(target) != 0) ok = false;
    if (!ok) { unlink(temporary); return false; }
    if (std::rename(path, backup) != 0) { unlink(temporary); return false; }
    if (std::rename(temporary, path) != 0) {
        std::rename(backup, path); unlink(temporary); return false;
    }
    return true;
}
}
