#include "hro_header.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>
#include <cctype>
namespace hro {
namespace {
using Json = nlohmann::json;
bool isoTime(const std::string& value, bool utc, bool hourOnly) {
    if (value.size() != (utc ? 20u : 19u) || (utc && value.back() != 'Z')) return false;
    for (std::size_t i = 0; i < 19; ++i) {
        char expected = 0;
        if (i == 4 || i == 7) expected = '-';
        if (i == 10) expected = 'T';
        if (i == 13 || i == 16) expected = ':';
        if (expected ? value[i] != expected : !std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    const auto n = [&](std::size_t at, std::size_t count) { return std::stoi(value.substr(at, count)); };
    const int year = n(0,4), month = n(5,2), day = n(8,2), hour = n(11,2), minute = n(14,2), second = n(17,2);
    const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 2024 || year >= 2100 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
    const int maximum = days[month-1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return day >= 1 && day <= maximum && (!hourOnly || (!minute && !second));
}
bool validate(const Json& j) {
    auto integer = [&](const char* key, std::int64_t expected) {
        return j.contains(key) && j[key].is_number_integer() && j[key] == expected;
    };
    auto text = [&](const char* key, const char* expected) {
        return j.contains(key) && j[key].is_string() && j[key] == expected;
    };
    const bool sequential = text("RECORD_LAYOUT", "SEQUENTIAL_FFT_V1");
    if (j.contains("RECORD_LAYOUT") && !sequential) return false;
    if (sequential && (!integer("RECORD_SIZE", 16 + BIN_COUNT * 4) ||
                       !integer("DATA_OFFSET", HEADER_SIZE) ||
                       !j.contains("RECORD_FIELDS") || j["RECORD_FIELDS"] !=
                       Json({"TIMESTAMP_MS_INT64", "SEQUENCE_UINT64", "FFT_FLOAT32"}))) return false;
    if (!j.is_object() || !integer("HRO_FILE_VERSION", 2) || !integer("HEADER_SIZE", HEADER_SIZE) ||
        !integer("VALIDITY_MAP_SIZE", sequential ? 0 : VALIDITY_MAP_SIZE) || !integer("EVENT_MAP_SIZE", sequential ? 0 : EVENT_MAP_SIZE) ||
        !integer("RECORD_SLOTS", sequential ? 0 : RECORD_SLOTS) || !integer("RECORD_INTERVAL_SEC", sequential ? 0 : 1) ||
        !integer("FFT_SIZE", FFT_SIZE) || !integer("FFT_BIN_COUNT", BIN_COUNT) ||
        !text("BYTE_ORDER", "LITTLE_ENDIAN") || !text("DATA_TYPE", "FLOAT32") ||
        !text("EVENT_TYPE", "UINT8_BIT_FLAGS") || !text("FFT_VALUE_SCALE", "DB_POWER") ||
        !text("FFT_NORMALIZATION", "NONE") || !text("FFT_WINDOW", "PERIODIC_HANN")) return false;
    for (const char* k : {"FILE_START_UTC", "FILE_HOUR_LOCAL", "TIME_ZONE", "START_TIME_SOURCE",
                         "NTP_SYNC_TIME_UTC", "OBSERVER", "LOCATION", "RECEIVER", "ANTENNA",
                         "DEVICE", "SOFTWARE", "SOFTWARE_VERSION"})
        if (!j.contains(k) || !j[k].is_string()) return false;
    if (!isoTime(j["FILE_START_UTC"], true, true) || !isoTime(j["FILE_HOUR_LOCAL"], false, true)) return false;
    const std::string sync = j["NTP_SYNC_TIME_UTC"];
    if (!sync.empty() && !isoTime(sync, true, false)) return false;
    const std::string source = j["START_TIME_SOURCE"];
    if (source != "NTP" && source != "RTC" && source != "PHONE" && source != "SYSTEM" && source != "UNKNOWN") return false;
    if (!j.contains("SDR_GAIN_TENTHS_DB") || !j["SDR_GAIN_TENTHS_DB"].is_number_integer()) return false;
    if (!j.contains("NTP_SYNCED") || !j["NTP_SYNCED"].is_boolean()) return false;
    for (const char* k : {"RF_FREQUENCY_HZ", "SDR_LO_FREQUENCY_HZ", "SDR_SAMPLE_RATE_HZ", "FFT_OUTPUT_RATE_HZ", "TIME_CONFLICTS"})
        if (!j.contains(k) || !j[k].is_number_integer() || j[k].get<double>() < 0) return false;
    if (j["TIME_CONFLICTS"].get<double>() > UINT32_MAX) return false;
    for (const char* k : {"LATITUDE", "LONGITUDE", "FFT_CENTER_HZ", "FFT_RANGE_HZ", "FFT_MIN_HZ", "FFT_MAX_HZ", "FFT_RESOLUTION_HZ", "FFT_POWER_EPSILON"})
        if (!j.contains(k) || !j[k].is_number() || !std::isfinite(j[k].get<double>())) return false;
    const double center = j["FFT_CENTER_HZ"], lo = j["FFT_MIN_HZ"], hi = j["FFT_MAX_HZ"];
    return j["FFT_RANGE_HZ"] == 300 && j["FFT_RESOLUTION_HZ"] == 1 &&
        j["FFT_OUTPUT_RATE_HZ"] == 8192 && j["FFT_POWER_EPSILON"] == 1e-20 &&
        std::floor(center) == center && lo == center - 300 && hi == center + 300 && lo >= 0 && hi <= 1800 &&
        std::abs(j["LATITUDE"].get<double>()) <= 90 && std::abs(j["LONGITUDE"].get<double>()) <= 180;
}
}
bool HroHeader::load(const std::array<char, HEADER_SIZE>& header) {
    valid_ = false; values_.clear();
    auto end = std::find(header.begin(), header.end(), '\0');
    if (end == header.end() || !std::all_of(end, header.end(), [](char c) { return c == 0; })) return false;
    try {
        // Each object gets its own key set, so duplicates inside nested extensions also fail.
        std::vector<std::set<std::string>> keys;
        bool duplicate = false;
        auto callback = [&](int, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key && !keys.back().insert(parsed.get<std::string>()).second) duplicate = true;
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        const auto j = Json::parse(header.begin(), end, callback);
        if (duplicate || !validate(j)) return false;
        for (auto it = j.begin(); it != j.end(); ++it)
            values_[it.key()] = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
        valid_ = true;
        return true;
    } catch (const Json::exception&) { return false; }
}
bool HroHeader::getValue(const std::string& key, std::string& value) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return false;
    value = it->second; return true;
}
bool HroHeader::isValidV2() const { return valid_; }
bool HroHeader::incrementTimeConflicts(std::array<char, HEADER_SIZE>& header) {
    HroHeader parsed; if (!parsed.load(header)) return false;
    try {
        const auto end = std::find(header.begin(), header.end(), 0);
        auto j = Json::parse(header.begin(), end);
        const auto value = j["TIME_CONFLICTS"].get<std::uint64_t>();
        if (value >= UINT32_MAX) return false;
        j["TIME_CONFLICTS"] = value + 1;
        const auto text = j.dump(2); if (text.size() >= HEADER_SIZE) return false;
        header.fill(0); std::copy(text.begin(), text.end(), header.begin()); return true;
    } catch (const Json::exception&) { return false; }
}
bool HroHeader::encode(const HroMetadata& m, std::array<char, HEADER_SIZE>& header) {
    try {
        const Json j = {
            {"HRO_FILE_VERSION", FILE_VERSION}, {"HEADER_SIZE", HEADER_SIZE},
            {"VALIDITY_MAP_SIZE", VALIDITY_MAP_SIZE}, {"EVENT_MAP_SIZE", EVENT_MAP_SIZE},
            {"EVENT_TYPE", "UINT8_BIT_FLAGS"}, {"DATA_TYPE", "FLOAT32"}, {"BYTE_ORDER", "LITTLE_ENDIAN"},
            {"FILE_START_UTC", m.fileStartUtc}, {"FILE_HOUR_LOCAL", m.fileHourLocal}, {"TIME_ZONE", m.timeZone},
            {"START_TIME_SOURCE", m.startTimeSource}, {"NTP_SYNCED", m.ntpSynced},
            {"NTP_SYNC_TIME_UTC", m.ntpSyncTimeUtc}, {"TIME_CONFLICTS", m.timeConflicts},
            {"OBSERVER", m.observer}, {"LOCATION", m.location}, {"LONGITUDE", m.longitude}, {"LATITUDE", m.latitude},
            {"RECEIVER", m.receiver}, {"ANTENNA", m.antenna}, {"RF_FREQUENCY_HZ", m.rfFrequencyHz},
            {"SDR_GAIN_TENTHS_DB", m.sdrGainTenthsDb}, {"SDR_LO_FREQUENCY_HZ", m.sdrLoFrequencyHz}, {"SDR_SAMPLE_RATE_HZ", m.sdrSampleRateHz},
            {"FFT_SIZE", FFT_SIZE}, {"FFT_OUTPUT_RATE_HZ", m.fftOutputRateHz}, {"FFT_RESOLUTION_HZ", m.fftResolutionHz},
            {"FFT_CENTER_HZ", m.fftCenterHz}, {"FFT_RANGE_HZ", m.fftRangeHz},
            {"FFT_MIN_HZ", m.fftCenterHz - m.fftRangeHz}, {"FFT_MAX_HZ", m.fftCenterHz + m.fftRangeHz},
            {"FFT_BIN_COUNT", BIN_COUNT}, {"RECORD_INTERVAL_SEC", RECORD_INTERVAL_SEC}, {"RECORD_SLOTS", RECORD_SLOTS},
            {"FFT_VALUE_SCALE", "DB_POWER"}, {"FFT_WINDOW", "PERIODIC_HANN"}, {"FFT_NORMALIZATION", "NONE"},
            {"FFT_POWER_EPSILON", 1e-20}, {"DEVICE", m.device}, {"SOFTWARE", m.software}, {"SOFTWARE_VERSION", m.softwareVersion}
        };
        if (!validate(j)) return false;
        const auto text = j.dump(2);
        if (text.size() >= HEADER_SIZE) return false;
        header.fill(0); std::copy(text.begin(), text.end(), header.begin());
        return true;
    } catch (const Json::exception&) { return false; }
}
}
