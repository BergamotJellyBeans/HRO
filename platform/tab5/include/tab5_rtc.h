#pragma once
#include <cstdint>

namespace hro::tab5::device {
// UTC calendar values. No M5Unified types leak into the application API.
struct RtcDateTime {
    struct Date { int16_t year; int8_t month; int8_t date; } date;
    struct Time { int8_t hours; int8_t minutes; int8_t seconds; } time;
};
bool rtc_enabled();
RtcDateTime read_rtc();
void write_rtc(const RtcDateTime& value);
}
