#pragma once
#include <cstddef>
#include <cstdint>
namespace hro::tab5 {
struct PhoneFieldData {
    std::int64_t unix_ms = 0;
    double latitude = 0;
    double longitude = 0;
};
bool decode_phone_field_data(const char* json, std::size_t length, PhoneFieldData& out);
}
