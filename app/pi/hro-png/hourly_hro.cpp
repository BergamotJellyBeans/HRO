#include "hourly_hro.hpp"
#include "hro_header.h"
#include "hro_version.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
namespace hro::pi {
namespace {
std::string date(std::int64_t seconds, const char* format, bool local = false) {
    const time_t t = seconds + (local ? 9 * 3600 : 0);
    tm utc{}; gmtime_r(&t, &utc);
    char text[80]{}; std::strftime(text, sizeof text, format, &utc); return text;
}
bool writeHeader(std::ofstream& file, const HroMetadata& metadata) {
    std::array<char, HEADER_SIZE> bytes{};
    if (!HroHeader::encode(metadata, bytes)) return false;
    auto header = nlohmann::json::parse(bytes.data());
    header["RECORD_LAYOUT"] = "SEQUENTIAL_FFT_V1";
    header["RECORD_SIZE"] = 16 + BIN_COUNT * 4;
    header["DATA_OFFSET"] = HEADER_SIZE;
    header["VALIDITY_MAP_SIZE"] = 0;
    header["EVENT_MAP_SIZE"] = 0;
    header["RECORD_SLOTS"] = 0;
    header["RECORD_INTERVAL_SEC"] = 0;
    header["RECORD_FIELDS"] = {"TIMESTAMP_MS_INT64", "SEQUENCE_UINT64", "FFT_FLOAT32"};
    const auto text = header.dump(2);
    if (text.size() >= bytes.size()) return false;
    bytes.fill(0); std::copy(text.begin(), text.end(), bytes.begin());
    file.write(bytes.data(), bytes.size()); file.flush();
    return bool(file);
}
std::string timeSource() {
    FILE* f = popen("timedatectl show --property=NTPSynchronized --value 2>/dev/null", "r");
    if (!f) return "UNKNOWN";
    char text[32]{}; const bool read = fgets(text, sizeof text, f) != nullptr;
    const int rc = pclose(f);
    if (!read || rc != 0) return "UNKNOWN";
    return std::string(text).find("yes") == 0 ? "NTP" : "SYSTEM";
}
}
HourlyHroRecorder::HourlyHroRecorder(std::filesystem::path directory, HroConfig config)
    : directory_(std::move(directory)), config_(std::move(config)) {}
HourlyHroRecorder::~HourlyHroRecorder() { stop(); }
bool HourlyHroRecorder::stop() {
    if (!file_.is_open()) return true;
    file_.flush(); const bool good = bool(file_); file_.close();
    if (!good || file_.fail()) return false;
    std::error_code ec;
    if (std::filesystem::exists(finalPath_, ec) || ec) return false;
    std::filesystem::rename(tempPath_, finalPath_, ec);
    return !ec;
}
bool HourlyHroRecorder::begin(std::int64_t hour) {
    const auto day = directory_ / date(hour, "%Y", true) / date(hour, "%Y%m", true) / date(hour, "%Y%m%d", true);
    std::filesystem::create_directories(day);
    const auto stem = config_.screenshot_prefix + date(hour, "%Y%m%d%H00", true);
    // Never mix layouts, overwrite files, or resume a possibly torn record.
    // Restart / clock reversal starts a numbered segment in the same directory.
    for (std::uint64_t part = 0;; ++part) {
        finalPath_ = day / (stem + (part ? ".part" + std::to_string(part) : "") + ".hro");
        tempPath_ = finalPath_.string() + ".tmp";
        if (!std::filesystem::exists(finalPath_) && !std::filesystem::exists(tempPath_)) break;
    }
    HroMetadata m;
    m.fileStartUtc = date(hour, "%Y-%m-%dT%H:%M:%SZ");
    m.fileHourLocal = date(hour, "%Y-%m-%dT%H:%M:%S", true); m.timeZone = "Asia/Tokyo";
    m.startTimeSource = timeSource(); m.ntpSynced = m.startTimeSource == "NTP";
    // timedatectl does not provide the last actual synchronization timestamp.
    m.ntpSyncTimeUtc = "";
    m.observer = config_.observer; m.location = config_.location;
    m.longitude = config_.longitude; m.latitude = config_.latitude;
    m.receiver = config_.receiver; m.antenna = config_.antenna;
    m.rfFrequencyHz = config_.frequency_hz;
    m.sdrSampleRateHz = 960000; m.sdrLoFrequencyHz = config_.frequency_hz + 960000 / 4;
    m.sdrGainTenthsDb = config_.sdr_gain;
    m.fftCenterHz = config_.fft_center_hz; m.fftRangeHz = FFT_RANGE_HZ;
    m.device = "Raspberry Pi 5"; m.software = SOFTWARE_NAME; m.softwareVersion = SOFTWARE_VERSION;
    file_.clear(); file_.open(tempPath_, std::ios::binary | std::ios::trunc);
    if (!file_ || !writeHeader(file_, m)) { if (file_.is_open()) file_.close(); return false; }
    hour_ = hour;
    std::cout << "HRO sequential recording: " << tempPath_ << '\n';
    return true;
}
bool HourlyHroRecorder::record(std::int64_t timestampMs, std::uint64_t sequence,
                              const std::array<float, BIN_COUNT>& bins) try {
    if (timestampMs < 1704067200000LL || timestampMs >= 4102444800000LL ||
        !std::all_of(bins.begin(), bins.end(), [](float f) { return std::isfinite(f); })) return false;
    const auto hour = timestampMs / 3600000 * 3600;
    if (file_.is_open() && hour != hour_ && !stop()) return false;
    if (!file_.is_open() && !begin(hour)) return false;
    std::array<char, 16 + BIN_COUNT * 4> bytes{};
    const auto put = [&](std::size_t at, std::uint64_t value, unsigned size) {
        for (unsigned i = 0; i < size; ++i) bytes[at+i] = static_cast<char>(value >> (8*i));
    };
    put(0, static_cast<std::uint64_t>(timestampMs), 8); put(8, sequence, 8);
    for (std::size_t i = 0; i < bins.size(); ++i) {
        std::uint32_t bits; std::memcpy(&bits, &bins[i], 4); put(16+i*4, bits, 4);
    }
    file_.write(bytes.data(), bytes.size()); file_.flush();
    return bool(file_);
} catch (const std::filesystem::filesystem_error& error) {
    std::cerr << "HRO filesystem error: " << error.what() << '\n'; return false;
}
}
