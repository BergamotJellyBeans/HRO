#include "tab5_terminal_config.hpp"
#include "hro_sdr_config.h"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace hro::tab5::app {
namespace {
const cJSON* field(const cJSON* item, const char* name)
{ return cJSON_GetObjectItemCaseSensitive(item, name); }
bool text(const cJSON* item, const char* name, char* out, std::size_t capacity)
{
    const auto* value = field(item, name);
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= capacity)
        return false;
    std::memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}
bool number(const cJSON* item, const char* name, double& out)
{
    const auto* value = field(item, name);
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble)) return false;
    out = value->valuedouble;
    return true;
}
bool integer(const cJSON* item, const char* name, int64_t& out)
{
    double v;
    if (!number(item, name, v) || std::trunc(v) != v || v < -2147483648.0 || v > 4294967295.0)
        return false;
    out = static_cast<int64_t>(v);
    return true;
}
}
bool decode_terminal_config(const char* data, std::size_t size, uint32_t request_id,
                            const Tab5Config& local, Tab5Config& output)
{
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(data, size, &end, false);
    if (!root) return false;
    while (end < data + size && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    if (end != data + size) { cJSON_Delete(root); return false; }
    auto parse = [&]() {
        // Ignore a delayed reply for an older registration request.
        int64_t value;
        const auto* type = field(root, "type");
        if (!cJSON_IsString(type) || strcmp(type->valuestring, "register_ack") != 0 ||
            !integer(root, "version", value) || value != 1 ||
            !integer(root, "request_id", value) || value != request_id ||
            !integer(root, "lease_seconds", value) || value != 15) return false;
        const auto* config = field(root, "config");
        const auto* system = field(config, "system");
        const auto* station = field(config, "station");
        const auto* receiver = field(config, "receiver");
        const auto* screenshot = field(config, "screenshot");
        Tab5Config candidate = local;
        if (!text(system, "display_text", candidate.source_system_info, sizeof(candidate.source_system_info)) ||
            !candidate.source_system_info[0] ||
            !text(station, "observer", candidate.observer, sizeof(candidate.observer)) ||
            !text(station, "location", candidate.location, sizeof(candidate.location)) ||
            !number(station, "latitude", candidate.latitude) ||
            !number(station, "longitude", candidate.longitude) ||
            !text(receiver, "receiver", candidate.receiver, sizeof(candidate.receiver)) ||
            !text(receiver, "antenna", candidate.antenna, sizeof(candidate.antenna)) ||
            !text(screenshot, "prefix", candidate.screenshot_prefix, sizeof(candidate.screenshot_prefix)))
            return false;
        if (!integer(receiver, "frequency_hz", value) || value < 1000000 || value > 2000000000)
            return false;
        candidate.frequency_hz = static_cast<uint32_t>(value);
        if (!integer(receiver, "fft_center_hz", value) || value < 300 || value > 1500)
            return false;
        candidate.fft_center_hz = static_cast<int32_t>(value);
        if (!integer(receiver, "fft_range_hz", value) || value != 300) return false;
        candidate.fft_range_hz = 300;
        if (!integer(receiver, "level_peak_range_hz", value) || value < 0 || value > 300)
            return false;
        candidate.level_average_range_hz = static_cast<int32_t>(value);
        if (!integer(receiver, "sdr_gain", value) || value < 0 || value > 496) return false;
        candidate.sdr_gain = static_cast<int>(value);
        if (std::find(hro::SDR_GAIN_VALUES.begin(), hro::SDR_GAIN_VALUES.end(), candidate.sdr_gain)
             == hro::SDR_GAIN_VALUES.end()) return false;
        // Pi5 supports 48.0 dB even though the Tab5 standalone driver does not.
        if (!validate_hro_config(candidate, nullptr, 0, true)) return false;
        output = candidate;
        return true;
    };
    const bool valid = parse();
    cJSON_Delete(root);
    return valid;
}
}
