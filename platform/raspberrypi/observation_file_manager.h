#pragma once

#include <string>

namespace hro {

enum class ObservationFileState
{
    NotFound,
    CurrentHour,
    OldHour,
    Invalid
};

// Examine an existing .hro.tmp file and determine
// whether it belongs to the current observation hour.
ObservationFileState checkObservationFile(
    const std::string& tempPath,
    const std::string& currentHourLocal);

} // namespace hro
