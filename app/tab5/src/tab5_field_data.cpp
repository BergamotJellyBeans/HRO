#include "tab5_field_data.hpp"
#include "cJSON.h"
#include <cmath>
namespace hro::tab5 {
bool decode_phone_field_data(const char* text, std::size_t length, PhoneFieldData& out)
{
    if (!text || !length || length > 512) return false;
    const char* end = nullptr;
    cJSON* json = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!json) return false;
    while (end < text + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    const auto* version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const auto* ms = cJSON_GetObjectItemCaseSensitive(json, "unix_ms");
    const auto* lat = cJSON_GetObjectItemCaseSensitive(json, "latitude");
    const auto* lon = cJSON_GetObjectItemCaseSensitive(json, "longitude");
    const bool valid = end == text + length && cJSON_IsObject(json) &&
        cJSON_IsNumber(version) && version->valuedouble == 1 &&
        cJSON_IsNumber(ms) && std::isfinite(ms->valuedouble) &&
        ms->valuedouble >= 1704067200000.0 && ms->valuedouble < 4102444800000.0 &&
        std::floor(ms->valuedouble) == ms->valuedouble &&
        cJSON_IsNumber(lat) && std::isfinite(lat->valuedouble) && std::abs(lat->valuedouble) <= 90 &&
        cJSON_IsNumber(lon) && std::isfinite(lon->valuedouble) && std::abs(lon->valuedouble) <= 180;
    if (valid) out = {static_cast<std::int64_t>(ms->valuedouble), lat->valuedouble, lon->valuedouble};
    cJSON_Delete(json);
    return valid;
}
}
