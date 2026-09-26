#include "hro_header.h"

#include <algorithm>

namespace hro {

bool HroHeader::load(const std::array<char, HEADER_SIZE>& header)
{
    cardCount_ = 0;

    for (auto& card : cards_) {
        card.clear();
    }

    for (std::size_t i = 0; i < HEADER_CARD_COUNT; ++i) {

        const char* begin =
            header.data() + i * HEADER_CARD_SIZE;

        std::string card(begin, HEADER_CARD_SIZE);

        // Remove SPACE padding.
        while (!card.empty() && card.back() == ' ') {
            card.pop_back();
        }

        if (card.empty()) {
            continue;
        }

        cards_[cardCount_++] = card;

        if (card == "END_HEADER") {
            return true;
        }
    }

    // END_HEADER was not found.
    cardCount_ = 0;
    return false;
}


bool HroHeader::getValue(const std::string& key,
                         std::string& value) const
{
    const std::string prefix = key + "=";

    for (std::size_t i = 0; i < cardCount_; ++i) {

        const std::string& card = cards_[i];

        if (card.compare(0, prefix.size(), prefix) == 0) {
            value = card.substr(prefix.size());
            return true;
        }
    }

    return false;
}

bool HroHeader::isValidV1() const
{
    auto requireValue =
        [this](const std::string& key,
               const std::string& expected) -> bool
    {
        std::string value;

        return getValue(key, value) &&
               value == expected;
    };

    return
        requireValue("HRO_FILE_VERSION", "1") &&
        requireValue("HEADER_SIZE", "8192") &&
        requireValue("VALIDITY_MAP_SIZE", "512") &&
        requireValue("DATA_TYPE", "FLOAT32") &&
        requireValue("BYTE_ORDER", "LITTLE_ENDIAN") &&
        requireValue("FFT_SIZE", "8192") &&
        requireValue("FFT_BIN_COUNT", "501") &&
        requireValue("RECORD_SLOTS", "3600") &&
        requireValue("EVENT_MAP_SIZE", "3600");
}

} // namespace hro

