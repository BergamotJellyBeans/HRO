#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace hro::archive {
// Read-only snapshots. Sequential files may grow; incomplete tail bytes are ignored.
nlohmann::json listImages(const std::filesystem::path& root);
nlohmann::json list(const std::filesystem::path& root);
nlohmann::json page(const std::filesystem::path& root, const std::string& relative,
                    std::size_t offset, std::size_t limit = 1200);
}
