#include "hro_metadata.h"
#include "hro_writer.h"
#include "observation_file_manager.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>

namespace fs = std::filesystem;

static bool createTempHro(const std::string& finalPath,
                          const std::string& fileHourLocal)
{
    hro::HroMetadata metadata;

    metadata.fileStartUtc     = "2026-09-27T00:00:00Z";
    metadata.fileHourLocal    = fileHourLocal;
    metadata.timeZone        = "Asia/Tokyo";
    metadata.startTimeSource = "RTC";

    metadata.observer = "TEST";
    metadata.location = "TEST";

    metadata.receiver          = "TEST";
    metadata.antenna           = "TEST";
    metadata.rfFrequencyHz     = 53750000;
    metadata.sdrLoFrequencyHz  = 53749000;
    metadata.sdrSampleRateHz   = 960000;

    metadata.device          = "TEST";
    metadata.software        = "HRO TEST";
    metadata.softwareVersion = "1";

    {
        hro::HroWriter writer;

        if (!writer.open(finalPath)) {
            return false;
        }

        if (!writer.writeHeader(metadata)) {
            return false;
        }

        // Do not call close().
        // Destructor leaves the .hro.tmp file in place.
    }

    return true;
}

static bool expectState(const std::string& name,
                        hro::ObservationFileState actual,
                        hro::ObservationFileState expected)
{
    if (actual != expected) {
        std::cerr << "FAILED: " << name << '\n';
        return false;
    }

    std::cout << "PASS: " << name << '\n';
    return true;
}

int main()
{
    const fs::path dir = "/tmp/hro_test_observation_file_manager";

    fs::remove_all(dir);
    fs::create_directories(dir);

    bool ok = true;

    // ------------------------------------------------------------
    // Current hour
    // ------------------------------------------------------------

    const std::string currentFinal =
        (dir / "current.hro").string();

    if (!createTempHro(currentFinal,
                       "2026-09-27T08:00:00")) {
        std::cerr << "FAILED: create current file\n";
        return 1;
    }

    ok &= expectState(
        "CurrentHour",
        hro::checkObservationFile(
            currentFinal + ".tmp",
            "2026-09-27T08:00:00"),
        hro::ObservationFileState::CurrentHour);

    // ------------------------------------------------------------
    // Old hour
    // ------------------------------------------------------------

    const std::string oldFinal =
        (dir / "old.hro").string();

    if (!createTempHro(oldFinal,
                       "2026-09-27T07:00:00")) {
        std::cerr << "FAILED: create old file\n";
        return 1;
    }

    ok &= expectState(
        "OldHour",
        hro::checkObservationFile(
            oldFinal + ".tmp",
            "2026-09-27T08:00:00"),
        hro::ObservationFileState::OldHour);

    // ------------------------------------------------------------
    // Future hour
    // ------------------------------------------------------------

    const std::string futureFinal =
        (dir / "future.hro").string();

    if (!createTempHro(futureFinal,
                       "2026-09-27T09:00:00")) {
        std::cerr << "FAILED: create future file\n";
        return 1;
    }

    ok &= expectState(
        "FutureHour",
        hro::checkObservationFile(
            futureFinal + ".tmp",
            "2026-09-27T08:00:00"),
        hro::ObservationFileState::FutureHour);

    // ------------------------------------------------------------
    // Invalid file
    // ------------------------------------------------------------

    const fs::path invalidPath = dir / "invalid.hro.tmp";

    {
        std::ofstream file(invalidPath, std::ios::binary);
        file << "INVALID";
    }

    ok &= expectState(
        "Invalid",
        hro::checkObservationFile(
            invalidPath.string(),
            "2026-09-27T08:00:00"),
        hro::ObservationFileState::Invalid);

    // ------------------------------------------------------------
    // Not found
    // ------------------------------------------------------------

    ok &= expectState(
        "NotFound",
        hro::checkObservationFile(
            (dir / "missing.hro.tmp").string(),
            "2026-09-27T08:00:00"),
        hro::ObservationFileState::NotFound);

    fs::remove_all(dir);

    return ok ? 0 : 1;
}
