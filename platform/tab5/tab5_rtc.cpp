#include "tab5_rtc.h"
#include <M5Unified.h>

namespace hro::tab5::device {
bool rtc_enabled() { return M5.Rtc.isEnabled(); }
RtcDateTime read_rtc()
{
    const auto value = M5.Rtc.getDateTime();
    return {{value.date.year, value.date.month, value.date.date},
            {value.time.hours, value.time.minutes, value.time.seconds}};
}
void write_rtc(const RtcDateTime& value)
{
    const m5::rtc_datetime_t datetime = {
        {value.date.year, value.date.month, value.date.date},
        {value.time.hours, value.time.minutes, value.time.seconds}
    };
    M5.Rtc.setDateTime(datetime);
}
}
