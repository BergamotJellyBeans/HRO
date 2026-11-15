#pragma once
#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>
namespace hro::archive {
// Thirty JST calendar days, ending just before the optional YYYYMMDD cursor.
nlohmann::json listWindow(const std::filesystem::path& root, bool images,
                          const std::string& before = "");
// Compact read-only 20-minute PNG-view frame: 16-byte header, 1200 validity bytes,
// then 1200 x 601 little-endian float32 bins. Missing seconds stay blank.
std::string imageFrame(const std::filesystem::path& root, const std::string& png);
// Read-only snapshots. Sequential files may grow; incomplete tail bytes are ignored.
nlohmann::json listImages(const std::filesystem::path& root);
nlohmann::json list(const std::filesystem::path& root);
nlohmann::json page(const std::filesystem::path& root, const std::string& relative,
                    std::size_t offset, std::size_t limit = 1200);
}
