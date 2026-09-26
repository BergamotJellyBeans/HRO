#include "observation_file_manager.h"

#include "hro_format.h"
#include "hro_header.h"

#include <array>
#include <filesystem>
#include <fstream>

namespace hro {

ObservationFileState checkObservationFile(
    const std::string& tempPath,
    const std::string& currentHourLocal)
{
    // File does not exist.
    std::error_code ec;

    if (!std::filesystem::exists(tempPath, ec)) {
        return ObservationFileState::NotFound;
    }

    if (ec) {
        return ObservationFileState::Invalid;
    }

    // Basic size validation.
    const auto size = std::filesystem::file_size(tempPath, ec);

    if (ec || size != FILE_SIZE) {
        return ObservationFileState::Invalid;
    }

    // Read fixed-size HRO header.
    std::ifstream file(tempPath, std::ios::binary);

    if (!file.is_open()) {
        return ObservationFileState::Invalid;
    }

    std::array<char, HEADER_SIZE> buffer{};

    file.read(
        buffer.data(),
        static_cast<std::streamsize>(buffer.size()));

    if (!file) {
        return ObservationFileState::Invalid;
    }

    HroHeader header;

    if (!header.load(buffer)) {
        return ObservationFileState::Invalid;
    }

    if (!header.isValidV1()) {
        return ObservationFileState::Invalid;
    }

    // Get the observation hour stored in the file.
    std::string fileHourLocal;

    if (!header.getValue("FILE_HOUR_LOCAL", fileHourLocal)) {
        return ObservationFileState::Invalid;
    }

    if (fileHourLocal == currentHourLocal) {
        return ObservationFileState::CurrentHour;
    }

    return ObservationFileState::OldHour;
}

} // namespace hro
