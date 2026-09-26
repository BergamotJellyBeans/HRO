#pragma once

#include <array>
#include <string>

#include "hro_format.h"

namespace hro {

class HroHeader
{
public:
    // Load and parse an HRO fixed-size header.
    bool load(const std::array<char, HEADER_SIZE>& header);

    // Returns the value associated with KEY.
    // Returns false if KEY does not exist.
    bool getValue(const std::string& key,
                  std::string& value) const;
    // Returns true if the header is compatible with HRO file format v1.
    bool isValidV1() const;
private:
    std::array<std::string, HEADER_CARD_COUNT> cards_{};
    std::size_t cardCount_ = 0;
};

} // namespace hro

