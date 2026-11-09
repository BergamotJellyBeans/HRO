#include "archive_hro.hpp"
#include "hro_header.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <ctime>
#include <sstream>
#include <iomanip>
namespace hro::archive {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
fs::path checked(const fs::path& root, const std::string& relative) {
    const fs::path input(relative);
    if (input.empty() || input.is_absolute()) throw std::invalid_argument("Invalid archive path");
    const auto base = fs::canonical(root);
    const auto path = fs::canonical(base / input);
    const auto local = path.lexically_relative(base);
    if (local.empty() || *local.begin() == "..") throw std::invalid_argument("Archive path outside observations");
    const auto name = path.filename().string();
    if (name.rfind("._", 0) == 0 ||
        !(path.extension() == ".hro" || (path.extension() == ".tmp" && path.stem().extension() == ".hro")))
        throw std::invalid_argument("Expected an HRO file");
    return path;
}
struct Snapshot {
    std::ifstream stream;
    Json header;
    bool sequential;
    std::uintmax_t size, count, tail;
    std::int64_t startMs;
    explicit Snapshot(const fs::path& path) : stream(path, std::ios::binary) {
        if (!stream) throw std::runtime_error("Cannot open HRO file; refresh the list if it was finalized");
        // Size from the open stream avoids a path lookup after .tmp -> .hro rename.
        stream.seekg(0, std::ios::end);
        const auto end = stream.tellg();
        if (end < static_cast<std::streamoff>(HEADER_SIZE)) throw std::runtime_error("Incomplete HRO header");
        size = static_cast<std::uintmax_t>(end);
        std::array<char, HEADER_SIZE> bytes{};
        stream.seekg(0); stream.read(bytes.data(), bytes.size());
        HroHeader parsed;
        if (!stream || !parsed.load(bytes)) throw std::runtime_error("Invalid or unsupported HRO header");
        header = Json::parse(bytes.data());
        sequential = header.value("RECORD_LAYOUT", "") == "SEQUENTIAL_FFT_V1";
        if (sequential) {
            count = (size - HEADER_SIZE) / 2420;
            tail = (size - HEADER_SIZE) % 2420;
        } else {
            // Legacy map offsets must exist; absent spectra are shown as missing.
            if (size < DATA_OFFSET || size > FILE_SIZE) throw std::runtime_error("Invalid legacy HRO size");
            count = RECORD_SLOTS;
            tail = size < FILE_SIZE ? FILE_SIZE - size : 0;
        }
        std::tm utc{};
        std::istringstream text(header.at("FILE_START_UTC").get<std::string>());
        text >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        if (text.fail()) throw std::runtime_error("Invalid HRO start time");
        startMs = static_cast<std::int64_t>(timegm(&utc)) * 1000;
    }
    Json info() const {
        return {{"header", header}, {"record_count", count}, {"trailing_bytes", tail},
                {"layout", sequential ? "sequential" : "legacy"}};
    }
    Json record(std::size_t index) {
        Json result = {{"index", index}, {"valid", false}, {"sequence", nullptr},
                       {"timestamp_ms", startMs + static_cast<std::int64_t>(index) * 1000}, {"fft_db", nullptr}};
        std::array<unsigned char, 2420> data{};
        std::size_t prefix = sequential ? 16 : 0;
        if (!sequential) {
            stream.clear(); stream.seekg(VALIDITY_MAP_OFFSET + validityByteIndex(index));
            unsigned char validity = 0; stream.read(reinterpret_cast<char*>(&validity), 1);
            if (!stream || !(validity & validityBitMask(index))) return result;
            stream.seekg(EVENT_MAP_OFFSET + index); unsigned char event = 0;
            stream.read(reinterpret_cast<char*>(&event), 1); result["visual"] = bool(event & EVENT_VISUAL);
        }
        const auto at = sequential ? HEADER_SIZE + index * 2420 : spectrumOffset(index);
        const auto length = prefix + BIN_COUNT * 4;
        if (at + length > size) { result["error"] = "Incomplete FFT record"; return result; }
        stream.clear(); stream.seekg(at);
        stream.read(reinterpret_cast<char*>(data.data()), length);
        if (!stream) { result["error"] = "FFT read failed"; return result; }
        const auto u64 = [&](std::size_t offset, unsigned width) {
            std::uint64_t value = 0;
            for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(data[offset+i]) << (8*i);
            return value;
        };
        if (sequential) {
            const auto timestamp = u64(0, 8);
            if (timestamp < 1704067200000ULL || timestamp >= 4102444800000ULL) {
                result["error"] = "Invalid FFT timestamp"; return result;
            }
            result["timestamp_ms"] = timestamp;
            // Strings preserve all 64 sequence bits in JavaScript.
            result["sequence"] = std::to_string(u64(8, 8));
        }
        Json bins = Json::array();
        for (std::size_t i = 0; i < BIN_COUNT; ++i) {
            const auto bits = static_cast<std::uint32_t>(u64(prefix+i*4, 4));
            float value; std::memcpy(&value, &bits, 4);
            if (!std::isfinite(value)) { result["error"] = "Non-finite FFT value"; return result; }
            bins.push_back(value);
        }
        result["fft_db"] = std::move(bins); result["valid"] = true;
        return result;
    }
};
}
Json listImages(const fs::path& root) {
    Json result = Json::array();
    if (!fs::exists(root)) return result;
    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".png" ||
            entry.path().filename().string().rfind("._", 0) == 0) continue;
        auto metadataPath = entry.path(); metadataPath.replace_extension(".json");
        const bool exists = fs::exists(metadataPath);
        Json item = {{"png_file", entry.path().filename().string()},
                     {"json_file", metadataPath.filename().string()}, {"json_exists", exists},
                     {"quality", "unknown"}};
        if (exists) {
            try {
                std::ifstream input(metadataPath); Json metadata; input >> metadata;
                if (metadata.contains("data_quality")) {
                    const auto& quality = metadata.at("data_quality");
                    const auto expected = quality.at("expected_seconds").get<int>();
                    const auto received = quality.at("received_seconds").get<int>();
                    if (expected <= 0 || received < 0 || received > expected)
                        throw std::runtime_error("Invalid quality counts");
                    item["quality"] = received < expected ? "partial" : "complete";
                    item["received_seconds"] = received; item["expected_seconds"] = expected;
                }
            } catch (const std::exception&) { item["quality"] = "metadata_error"; }
        }
        result.push_back(std::move(item));
    }
    std::sort(result.begin(), result.end(), [](const Json& a, const Json& b) { return a["png_file"] < b["png_file"]; });
    return result;
}
Json list(const fs::path& root) {
    Json items = Json::array();
    if (!fs::exists(root)) return items;
    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        const auto path = entry.path();
        if (!(path.extension() == ".hro" || (path.extension() == ".tmp" && path.stem().extension() == ".hro")) ||
            path.filename().string().rfind("._", 0) == 0) continue;
        const auto relative = path.lexically_relative(root).generic_string();
        Json item = {{"file", relative}, {"name", path.filename().string()}, {"recording", path.extension() == ".tmp"}};
        try {
            Snapshot snapshot(checked(root, relative));
            item.update(snapshot.info());
        } catch (const std::exception& e) { item["error"] = e.what(); }
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const Json& a, const Json& b) { return a["file"] < b["file"]; });
    return items;
}
namespace {
std::string calendar(std::int64_t day, const char* format = "%Y%m%d") {
    const time_t t = day * 86400; std::tm utc{}; gmtime_r(&t, &utc);
    char text[32]{}; std::strftime(text, sizeof text, format, &utc); return text;
}
std::int64_t parseDay(const std::string& date) {
    if (date.size() != 8 || !std::all_of(date.begin(), date.end(), [](char c){return c >= '0' && c <= '9';}))
        throw std::invalid_argument("Expected YYYYMMDD date cursor");
    std::tm utc{}; utc.tm_year = std::stoi(date.substr(0,4))-1900;
    utc.tm_mon = std::stoi(date.substr(4,2))-1; utc.tm_mday = std::stoi(date.substr(6,2));
    const auto day = static_cast<std::int64_t>(timegm(&utc)) / 86400;
    if (calendar(day) != date || utc.tm_year < 70 || utc.tm_year > 300)
        throw std::invalid_argument("Invalid calendar date cursor");
    return day;
}
}
Json listWindow(const fs::path& root, bool images, const std::string& before) {
    const auto end = before.empty() ? (static_cast<std::int64_t>(std::time(nullptr))+9*3600)/86400+1 : parseDay(before);
    const auto start = end-30;
    Json result = {{"items", Json::array()}, {"start_date", calendar(start, "%Y-%m-%d")},
                   {"end_date", calendar(end-1, "%Y-%m-%d")}, {"before", calendar(end)}, {"next_before", calendar(start)}, {"has_more", false}};
    if (!fs::exists(root)) return result;
    // Inspect date directories only. Older metadata and FFT files are not opened.
    std::vector<fs::path> days;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied), last; it != last; ++it) {
        if (it->is_symlink()) { it.disable_recursion_pending(); continue; }
        if (!it->is_directory()) continue;
        const auto name = it->path().filename().string();
        const int depth = it.depth();
        if (depth < 2) {
            if (name.size() != (depth == 0 ? 4u : 6u) ||
                !std::all_of(name.begin(), name.end(), [](char c){return c >= '0' && c <= '9';}))
                it.disable_recursion_pending();
            continue;
        }
        it.disable_recursion_pending();
        if (depth != 2 || name.size()!=8) continue;
        std::int64_t day;
        try { day = parseDay(name); } catch (const std::invalid_argument&) { continue; }
        if (it->path().parent_path().filename() != name.substr(0,6) ||
            it->path().parent_path().parent_path().filename() != name.substr(0,4)) continue;
        if (day < start) {
            // Only check names to avoid offering an Older button for empty directories.
            for (const auto& file : fs::directory_iterator(it->path(), fs::directory_options::skip_permission_denied)) {
                const auto path=file.path();
                if (file.is_symlink() || !file.is_regular_file() || path.filename().string().rfind("._",0)==0) continue;
                if ((images && path.extension()==".png") || (!images && (path.extension()==".hro" ||
                    (path.extension()==".tmp" && path.stem().extension()==".hro")))) {
                    result["has_more"]=true; break;
                }
            }
        } else if (day < end) days.push_back(it->path());
    }
    std::sort(days.begin(),days.end());
    for (const auto& day : days) {
        auto items = images ? listImages(day) : list(day);
        for (auto& item : items) {
            if (!images) item["file"] = (day.lexically_relative(root)/item["file"].get<std::string>()).generic_string();
            result["items"].push_back(std::move(item));
        }
    }
    return result;
}
Json page(const fs::path& root, const std::string& relative, std::size_t offset, std::size_t limit) {
    if (!limit || limit > 1200) throw std::invalid_argument("Page limit must be 1..1200");
    Snapshot snapshot(checked(root, relative));
    if (offset > snapshot.count) throw std::invalid_argument("Record offset out of range");
    Json result = snapshot.info(); result["offset"] = offset; result["records"] = Json::array();
    const auto end = offset + std::min<std::uintmax_t>(limit, snapshot.count - offset);
    for (auto i = offset; i < end; ++i) result["records"].push_back(snapshot.record(i));
    result["next_offset"] = end; result["has_more"] = end < snapshot.count;
    result["recording"] = fs::path(relative).extension() == ".tmp";
    return result;
}
}
