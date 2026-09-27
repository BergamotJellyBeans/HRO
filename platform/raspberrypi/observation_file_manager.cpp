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

    if (fileHourLocal < currentHourLocal) {
        return ObservationFileState::OldHour;
    }

    return ObservationFileState::FutureHour;
}

FinalizeResult finalizeOldObservationFile(
    const std::string& tempPath,
    const std::string& currentHourLocal)
{
    // Only a valid file belonging to a past hour may be finalized.
    const auto state =
        checkObservationFile(tempPath, currentHourLocal);

    if (state == ObservationFileState::NotFound) {
        return FinalizeResult::SourceNotFound;
    }

    if (state != ObservationFileState::OldHour) {
        return FinalizeResult::InvalidSource;
    }

    // Temporary file name must end in ".tmp".
    constexpr const char* suffix = ".tmp";

    if (tempPath.size() < 4 ||
        tempPath.compare(tempPath.size() - 4, 4, suffix) != 0) {
        return FinalizeResult::InvalidSource;
    }

    // Remove ".tmp" to obtain the final .hro path.
    const std::string finalPath =
        tempPath.substr(0, tempPath.size() - 4);

    std::error_code ec;

    // Never overwrite an existing finalized observation file.
    if (std::filesystem::exists(finalPath, ec)) {
        if (ec) {
            return FinalizeResult::RenameFailed;
        }

        return FinalizeResult::FinalFileExists;
    }

    if (ec) {
        return FinalizeResult::RenameFailed;
    }

    // Rename is performed only after all validation has succeeded.
    std::filesystem::rename(tempPath, finalPath, ec);

    if (ec) {
        return FinalizeResult::RenameFailed;
    }

    return FinalizeResult::Success;
}

} // namespace hro
