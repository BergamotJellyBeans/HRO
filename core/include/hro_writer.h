#pragma once

#include "hro_metadata.h"
#include "hro_format.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace hro {

class HroWriter
{
public:
    HroWriter() = default;
    ~HroWriter();

    HroWriter(const HroWriter&) = delete;
    HroWriter& operator=(const HroWriter&) = delete;

    // Create a new temporary HRO file.
    // finalPath : final ".hro" file name
    // Writing is performed to "finalPath + .tmp".
    bool open(const std::string& finalPath);

    // Resume writing to an existing ".hro.tmp" file.
    //
    // The file layout and header are validated before it is accepted.
    // Missing slots remain invalid and writing can continue at the
    // slot corresponding to the current observation time.
    bool resume(const std::string& tempPath);

    // Write the complete HRO v2 header from observation metadata.
    bool writeHeader(const HroMetadata& metadata);

    // Write one spectrum record to the specified one-second slot.
    bool writeSpectrum(std::size_t slot,
                       const SpectrumValue* spectrum);

    // Set an event flag for the specified slot.
    bool setEvent(std::size_t slot,
                  EventValue event);

    // Finish the file and rename .tmp -> .hro.
    bool close();

    bool isOpen() const;
    bool flush();
    bool noteTimeConflict();
    bool hasSpectrum(std::size_t slot);

private:
    bool setValidity(std::size_t slot);

    std::fstream file_;

    std::string finalPath_;
    std::string tempPath_;


    bool headerFinished_ = false;
    bool open_ = false;
};

} // namespace hro
