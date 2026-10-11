#include "tab5_visual_cache.hpp"
#include "hro_time_packet.h"
#include "tab5_visual_log_record.hpp"
#include "tab5_visual_csv_upgrade.hpp"
#include "tab5_visual_marker_history.hpp"
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
    using namespace hro::visual;
    Request r;
    const char* wire = "VISUAL 1 d263c4 0123456789abcdef";
    assert(parse(wire, std::strlen(wire), r));
    assert(std::strcmp(r.stick_id, "D263C4") == 0);
    assert(std::strcmp(r.event_id, "0123456789ABCDEF") == 0);
    assert(!parse(wire, 31, r)); assert(!parse(wire, 33, r));
    assert(!parse("VISUAL 2 D263C4 0123456789ABCDEF", 32, r));
    assert(!parse("VISUAL 1 D263CG 0123456789ABCDEF", 32, r));
    assert(!parse("VISUAL 1 D263C4 0123456789ABCDEG", 32, r));
    char embedded[32]; std::memcpy(embedded, wire, 32); embedded[20] = 0;
    assert(!parse(embedded, 32, r));
    char reply[80]; assert(ack(reply, sizeof(reply), r, 1234) > 0);
    assert(std::strcmp(reply, "ACK 1 D263C4 0123456789ABCDEF 1234") == 0);
    assert(matches_ack(reply, std::strlen(reply), r));
    assert(matches_ack("ACK 1 D263C4 0123456789ABCDEF 0", 31, r));
    assert(!matches_ack("ACK 1 D25ECC 0123456789ABCDEF 0", 31, r));
    assert(!matches_ack("ACK 1 D263C4 0123456789ABCDEF 0\n", 32, r));
    assert(!matches_ack("NACK 1 D263C4 0123456789ABCDEF BUSY", 34, r));
    Request mismatch = r; std::strcpy(mismatch.event_id, "FFFFFFFFFFFFFFFF");
    assert(!matches_ack(reply, std::strlen(reply), mismatch));
    assert(!matches_ack(nullptr, 31, r));
    assert(!matches_ack("ACK 1 D263C4 0123456789ABCDEF -1", 32, r));
    assert(!matches_ack("ACK 1 D263C4 0123456789ABCDEF 18446744073709551616", 50, r));
    assert(!matches_ack(reply, std::strlen(reply) - 4, r));
    assert(ack(reply, 5, r, 1234) == -1);
    char nonce[17];
    assert(hro::clock::token("TIME 1 0123456789abcdef", 23, nonce));
    assert(std::strcmp(nonce, "0123456789ABCDEF") == 0);
    assert(!hro::clock::token("TIME 2 0123456789abcdef", 23, nonce));
    assert(!hro::clock::token("TIME 1 0123456789abcdeg", 23, nonce));
    assert(!hro::clock::token("TIME 1 0123456789abcdef", 22, nonce));
    std::uint64_t clock_ms = 7;
    const int time_length = hro::clock::response(reply, sizeof(reply), nonce, hro::clock::MIN_MS);
    assert(hro::clock::parse(reply, time_length, nonce, clock_ms) && clock_ms == hro::clock::MIN_MS);
    assert(!hro::clock::parse(reply, time_length, "FFFFFFFFFFFFFFFF", clock_ms));
    assert(!hro::clock::parse(nullptr, 25, nonce, clock_ms));
    assert(!hro::clock::parse(reply, time_length - 1, nonce, clock_ms));
    const int unknown = hro::clock::response(reply, sizeof(reply), nonce, 0);
    assert(hro::clock::parse(reply, unknown, nonce, clock_ms) && clock_ms == 0);
    const int future = hro::clock::response(reply, sizeof(reply), nonce, hro::clock::MAX_MS);
    assert(!hro::clock::parse(reply, future, nonce, clock_ms));
    char busy[48];
    const int busy_size = std::snprintf(busy, sizeof(busy), "NACK 1 %s %s BUSY", r.stick_id, r.event_id);
    assert(matches_busy(busy, busy_size, r));
    assert(!matches_busy(busy, busy_size, mismatch));
    assert(!matches_busy(busy, busy_size - 1, r));
    assert(!matches_busy(nullptr, busy_size, r));
    using Cache = hro::tab5::app::VisualCache;
    Cache cache;
    std::uint64_t stamp = 0;
    assert(cache.accept(r, 100, 1234, stamp) == Cache::Result::New);
    assert(cache.accept(r, 200, 9999, stamp) == Cache::Result::Duplicate && stamp == 1234);
    Request other = r; std::strcpy(other.stick_id, "D25ECC");
    assert(cache.accept(other, 300, 5678, stamp) == Cache::Result::New);
    for (unsigned i = 0; i < Cache::CAPACITY - 2; ++i) {
        Request next = r; std::snprintf(next.event_id, sizeof(next.event_id), "%016X", i);
        assert(cache.accept(next, 400, 1, stamp) == Cache::Result::New);
    }
    Request extra = r; std::strcpy(extra.event_id, "FFFFFFFFFFFFFFFF");
    assert(cache.accept(extra, 500, 2, stamp) == Cache::Result::Full);
    assert(cache.accept(r, 600, 3, stamp) == Cache::Result::Duplicate && stamp == 1234);
    assert(cache.accept(extra, Cache::RETENTION_US + 400, 4, stamp) == Cache::Result::New);
    // Two Sticks at 1 event/sec each for 20 minutes must never fill the cache.
    Cache continuous;
    for (unsigned second = 0; second < 1200; ++second) {
        for (unsigned device = 0; device < 2; ++device) {
            Request event = r;
            std::strcpy(event.stick_id, device ? "D25ECC" : "D263C4");
            std::snprintf(event.event_id, sizeof(event.event_id), "%016X", second);
            const auto now = static_cast<std::int64_t>(second) * 1000000;
            assert(continuous.accept(event, now, second, stamp) == Cache::Result::New);
            assert(continuous.accept(event, now + 100, 99999, stamp) == Cache::Result::Duplicate);
            assert(stamp == second);
        }
    }

    Cache rollback;
    assert(rollback.accept(r, 1, 100, stamp) == Cache::Result::New);
    rollback.forget(r);
    assert(rollback.accept(r, 2, 200, stamp) == Cache::Result::New && stamp == 200);
    hro::tab5::app::VisualLogRecord record{r, 1577836800123ULL, 1234567};
    char row[128];
    assert(hro::tab5::app::visual_csv_row(row, sizeof(row), record) > 0);
    assert(std::strcmp(row, "2020-01-01 09:00:00.123,1577836800123,1234,D263C4,0123456789ABCDEF,,1,0\n") == 0);
    record.utc_ms = 0;
    assert(hro::tab5::app::visual_csv_row(row, sizeof(row), record) > 0);
    assert(std::strcmp(row, "UNSYNC,0,1234,D263C4,0123456789ABCDEF,,1,\n") == 0);
    assert(hro::tab5::app::visual_csv_row(row, 10, record) == -1);

    Request numbered;
    const char* counted = "VISUAL 2 D263C4 0123456789ABCDEF 0000000001";
    assert(parse(counted, std::strlen(counted), numbered));
    assert(numbered.version == 2 && numbered.sequence == 1);
    assert(!parse("VISUAL 2 D263C4 0123456789ABCDEF 0000000000", 43, numbered));
    assert(!parse("VISUAL 2 D263C4 0123456789ABCDEF 4294967296", 43, numbered));
    assert(!parse("VISUAL 2 D263C4 0123456789ABCDEF 000000000X", 43, numbered));
    assert(!parse(counted, 42, numbered));
    const int ack_size = ack(reply, sizeof(reply), numbered, 1234);
    assert(matches_ack(reply, ack_size, numbered));
    Request wrong_count = numbered; wrong_count.sequence = 2;
    assert(!matches_ack(reply, ack_size, wrong_count));
    const int numbered_busy_size = busy_reply(reply, sizeof(reply), numbered);
    assert(matches_busy(reply, numbered_busy_size, numbered));
    assert(!matches_busy(reply, numbered_busy_size, wrong_count));
    record.request = numbered;
    assert(hro::tab5::app::visual_csv_row(row, sizeof(row), record) > 0);
    assert(std::strcmp(row, "UNSYNC,0,1234,D263C4,0123456789ABCDEF,1,1,\n") == 0);
    Cache numbered_cache;
    assert(numbered_cache.accept(numbered, 1, 100, stamp) == Cache::Result::New);
    assert(numbered_cache.accept(numbered, 2, 200, stamp) == Cache::Result::Duplicate && stamp == 100);
    // A fresh random event ID on the next boot avoids collisions after resetting count.
    std::strcpy(numbered.event_id, "FEDCBA9876543210");
    assert(numbered_cache.accept(numbered, 3, 300, stamp) == Cache::Result::New);

    char csv_name[64];
    assert(hro::tab5::app::visual_csv_filename("HRO202610091800.png", csv_name, sizeof(csv_name)));
    assert(std::strcmp(csv_name, "HRO202610091800.csv") == 0);
    assert(!hro::tab5::app::visual_csv_filename("HRO202610091800.png", csv_name, 10));
    assert(!hro::tab5::app::visual_csv_filename("wrong.txt", csv_name, sizeof(csv_name)));
    // Choose a block by receive timestamp, even if writing is delayed past a boundary.
    record.utc_ms = 1577837999999ULL; // 2020-01-01 00:19:59.999 UTC
    assert(hro::tab5::app::visual_csv_block(record) == 1577836800);
    record.utc_ms = 1577838000000ULL; // 00:20:00 UTC
    assert(hro::tab5::app::visual_csv_block(record) == 1577838000);
    record.utc_ms = 1577923199999ULL; // 23:59:59.999 UTC
    assert(hro::tab5::app::visual_csv_block(record) == 1577922000);
    record.utc_ms = 1577923200000ULL; // next day
    assert(hro::tab5::app::visual_csv_block(record) == 1577923200);

    Request grouped;
    const char* triple = "VISUAL 3 D263C4 0123456789ABCDEF 3 0000000123";
    assert(parse(triple, std::strlen(triple), grouped));
    assert(grouped.version == 3 && grouped.meteor_count == 3 && grouped.sequence == 123);
    char packet[64];
    assert(encode(packet, sizeof(packet), grouped) == static_cast<int>(std::strlen(triple)));
    assert(!std::strcmp(packet, triple));
    assert(!parse("VISUAL 3 D263C4 0123456789ABCDEF 0 0000000123", 45, grouped));
    assert(!parse("VISUAL 3 D263C4 0123456789ABCDEF X 0000000123", 45, grouped));
    assert(!parse(triple, std::strlen(triple)-1, grouped));
    const int grouped_ack_size = ack(reply, sizeof(reply), grouped, 1234);
    assert(matches_ack(reply, grouped_ack_size, grouped));
    Request wrong_meteors = grouped; wrong_meteors.meteor_count = 2;
    assert(!matches_ack(reply, grouped_ack_size, wrong_meteors));
    assert(matches_busy(reply, busy_reply(reply, sizeof(reply), grouped), grouped));
    record.request = grouped;
    record.utc_ms = 0;
    assert(hro::tab5::app::visual_csv_row(row, sizeof(row), record) > 0);
    assert(!std::strcmp(row, "UNSYNC,0,1234,D263C4,0123456789ABCDEF,123,3,\n"));
    char csv_path[] = "/tmp/visual-upgrade-XXXXXX";
    const int fd = mkstemp(csv_path); assert(fd >= 0); close(fd);
    FILE* old_csv = std::fopen(csv_path, "w"); assert(old_csv);
    std::fputs("received_utc,received_unix_ms,tab5_uptime_ms,stick_id,event_id,stick_count\n", old_csv);
    std::fputs("UNSYNC,0,1234,D263C4,0123456789ABCDEF,1\n", old_csv);
    std::fclose(old_csv);
    assert(hro::tab5::app::visual_csv_upgrade(csv_path));
    assert(hro::tab5::app::visual_csv_upgrade(csv_path)); // Idempotent on the new format.
    FILE* upgraded = std::fopen(csv_path, "r"); assert(upgraded);
    char csv_line[256];
    assert(std::fgets(csv_line, sizeof(csv_line), upgraded));
    assert(!std::strcmp(csv_line, hro::tab5::app::VISUAL_CSV_HEADER));
    assert(std::fgets(csv_line, sizeof(csv_line), upgraded));
    assert(!std::strcmp(csv_line, "UNSYNC,0,1234,D263C4,0123456789ABCDEF,1,1,\n"));
    std::fclose(upgraded);
    char backup[192]; std::snprintf(backup, sizeof(backup), "%s.utc6", csv_path);
    assert(access(backup, F_OK) == 0);
    unlink(csv_path); unlink(backup);

    hro::tab5::app::VisualMarkerHistory markers;
    const std::int64_t start = 1577836800;
    markers.add(start * 1000, 1);
    markers.add(start * 1000 + 999, 2);
    assert(markers.at(start) == 3);
    assert(markers.column(1199, start) == 3);
    assert(markers.column(1198, start + 1) == 3);
    assert(markers.column(0, start + 1199) == 3);
    assert(markers.column(0, start + 1200) == 0);
    assert(markers.column(-1, start) == 0);
    assert(markers.column(1200, start) == 0);
    markers.add(0, 1);
    assert(markers.at(0) == 0);
    const auto retention = hro::tab5::app::VisualMarkerHistory::CAPACITY;
    markers.add((start + retention) * 1000, 1);
    assert(markers.at(start) == 0);
    assert(markers.at(start + retention) == 1);
    markers.add((start + retention) * 1000, UINT32_MAX);
    assert(markers.at(start + retention) == UINT32_MAX);

    using Marker = hro::tab5::app::VisualMarkerHistory;
    assert(Marker::height(0) == 0);
    assert(Marker::height(1) == 4);
    assert(Marker::height(2) == 8);
    assert(Marker::height(3) == 12);
    assert(Marker::height(UINT32_MAX) == hro::plot::WATERFALL_HEIGHT);

    char jst[32], block_second[8];
    assert(hro::tab5::app::visual_csv_time_fields(1791532932306ULL, jst, sizeof(jst), block_second, sizeof(block_second)));
    assert(!std::strcmp(jst, "2026-10-09 17:02:12.306"));
    assert(!std::strcmp(block_second, "132"));
    assert(hro::tab5::app::visual_csv_time_fields(1577837999999ULL, jst, sizeof(jst), block_second, sizeof(block_second)));
    assert(!std::strcmp(block_second, "1199"));
    assert(hro::tab5::app::visual_csv_time_fields(1577838000000ULL, jst, sizeof(jst), block_second, sizeof(block_second)));
    assert(!std::strcmp(block_second, "0"));
    assert(hro::tab5::app::visual_csv_time_fields(1577890800000ULL, jst, sizeof(jst), block_second, sizeof(block_second)));
    assert(!std::strcmp(jst, "2020-01-02 00:00:00.000"));
    char csv7_path[] = "/tmp/visual-upgrade7-XXXXXX";
    const int fd7 = mkstemp(csv7_path); assert(fd7 >= 0); close(fd7);
    FILE* old_csv7 = std::fopen(csv7_path, "w"); assert(old_csv7);
    std::fputs("received_utc,received_unix_ms,tab5_uptime_ms,stick_id,event_id,stick_count,meteor_count\n", old_csv7);
    std::fputs("2026-10-09T08:02:12.306Z,1791532932306,702487,D263C4,0123456789ABCDEF,1,3\n", old_csv7);
    std::fclose(old_csv7);
    assert(hro::tab5::app::visual_csv_upgrade(csv7_path));
    FILE* converted7 = std::fopen(csv7_path, "r"); assert(converted7);
    assert(std::fgets(csv_line, sizeof(csv_line), converted7));
    assert(!std::strcmp(csv_line, hro::tab5::app::VISUAL_CSV_HEADER));
    assert(std::fgets(csv_line, sizeof(csv_line), converted7));
    assert(!std::strcmp(csv_line, "2026-10-09 17:02:12.306,1791532932306,702487,D263C4,0123456789ABCDEF,1,3,132\n"));
    std::fclose(converted7);
    std::snprintf(backup, sizeof(backup), "%s.utc7", csv7_path);
    unlink(csv7_path); unlink(backup);

}
