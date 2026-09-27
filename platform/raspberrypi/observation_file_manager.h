#pragma once

#include <string>

namespace hro {

enum class ObservationFileState
{
    NotFound,
    CurrentHour,
    OldHour,
    FutureHour,
    Invalid
};

enum class FinalizeResult
{
    Success,
    SourceNotFound,
    InvalidSource,
    FinalFileExists,
    RenameFailed
};

// Examine an existing .hro.tmp file and determine
// whether it belongs to the current observation hour.
ObservationFileState checkObservationFile(
    const std::string& tempPath,
    const std::string& currentHourLocal);

// Safely finalize an old .hro.tmp file.
//
// The source file is validated before rename.
// An existing final .hro file is never overwritten.
FinalizeResult finalizeOldObservationFile(
    const std::string& tempPath,
    const std::string& currentHourLocal);
    
} // namespace hro
