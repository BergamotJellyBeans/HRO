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
