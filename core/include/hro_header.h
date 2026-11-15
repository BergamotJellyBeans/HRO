#pragma once
#include <array>
#include <map>
#include <string>
#include "hro_format.h"
#include "hro_metadata.h"
namespace hro {
class HroHeader {
public:
    bool load(const std::array<char, HEADER_SIZE>& header);
    bool getValue(const std::string& key, std::string& value) const;
    bool isValidV2() const;
    static bool incrementTimeConflicts(std::array<char, HEADER_SIZE>& header);
    static bool encode(const HroMetadata& metadata, std::array<char, HEADER_SIZE>& header);
private:
    std::map<std::string, std::string> values_;
    bool valid_ = false;
};
}
