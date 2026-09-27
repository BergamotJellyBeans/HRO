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

    // ------------------------------------------------------------
    // Finalize old observation file
    // ------------------------------------------------------------

    const std::string finalizeFinal =
        (dir / "finalize_old.hro").string();

    if (!createTempHro(finalizeFinal,
            "2026-09-27T07:00:00")) {
        std::cerr << "FAILED: create finalize old file\n";
        return 1;
    }

    const std::string finalizeTemp = finalizeFinal + ".tmp";

    const auto finalizeResult =
        hro::finalizeOldObservationFile(
            finalizeTemp,
            "2026-09-27T08:00:00");

    if (finalizeResult != hro::FinalizeResult::Success) {
        std::cerr << "FAILED: finalize OldHour\n";
        ok = false;
    }
    else if (fs::exists(finalizeTemp)) {
        std::cerr << "FAILED: temporary file still exists\n";
        ok = false;
    }
    else if (!fs::exists(finalizeFinal)) {
        std::cerr << "FAILED: final file does not exist\n";
        ok = false;
    }
    else {
        std::cout << "PASS: FinalizeOldHour\n";
    }

    // ------------------------------------------------------------
    // Never overwrite an existing final file
    // ------------------------------------------------------------

    const std::string protectedFinal =
        (dir / "protected.hro").string();

    if (!createTempHro(protectedFinal,
                       "2026-09-27T07:00:00")) {
        std::cerr << "FAILED: create protected temp file\n";
        return 1;
    }

    const std::string protectedTemp = protectedFinal + ".tmp";

    // Create an existing final file with known contents.
    const std::string protectedContents = "DO NOT OVERWRITE";

    {
        std::ofstream file(protectedFinal, std::ios::binary);
        file << protectedContents;
    }

    const auto protectedResult =
        hro::finalizeOldObservationFile(
            protectedTemp,
            "2026-09-27T08:00:00");

    if (protectedResult != hro::FinalizeResult::FinalFileExists) {
        std::cerr << "FAILED: existing final file not detected\n";
        ok = false;
    }
    else if (!fs::exists(protectedTemp)) {
        std::cerr << "FAILED: temporary file was removed\n";
        ok = false;
    }
    else if (!fs::exists(protectedFinal)) {
        std::cerr << "FAILED: existing final file disappeared\n";
        ok = false;
    }
    else {
        std::ifstream file(protectedFinal, std::ios::binary);
        std::string contents(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        if (contents != protectedContents) {
            std::cerr << "FAILED: existing final file was modified\n";
            ok = false;
        }
        else {
            std::cout << "PASS: ProtectExistingFinalFile\n";
        }
    }

    // ------------------------------------------------------------
    // Reject CurrentHour
    // ------------------------------------------------------------

    const std::string currentFinalize =
        (dir / "finalize_current.hro").string();

    if (!createTempHro(currentFinalize,
                   "2026-09-27T08:00:00")) {
        std::cerr << "FAILED: create current finalize file\n";
        return 1;
    }

    const std::string currentFinalizeTemp =
        currentFinalize + ".tmp";

    const auto currentFinalizeResult =
        hro::finalizeOldObservationFile(
            currentFinalizeTemp,
            "2026-09-27T08:00:00");

    if (currentFinalizeResult != hro::FinalizeResult::InvalidSource ||
        !fs::exists(currentFinalizeTemp) ||
        fs::exists(currentFinalize)) {

        std::cerr << "FAILED: CurrentHour finalize protection\n";
        ok = false;
    }
    else {
        std::cout << "PASS: RejectCurrentHourFinalize\n";
    }


    // ------------------------------------------------------------
    // Reject FutureHour
    // ------------------------------------------------------------

    const std::string futureFinalize =
        (dir / "finalize_future.hro").string();

    if (!createTempHro(futureFinalize,
                   "2026-09-27T09:00:00")) {
        std::cerr << "FAILED: create future finalize file\n";
        return 1;
    }

    const std::string futureFinalizeTemp =
        futureFinalize + ".tmp";

    const auto futureFinalizeResult =
        hro::finalizeOldObservationFile(
            futureFinalizeTemp,
            "2026-09-27T08:00:00");

    if (futureFinalizeResult != hro::FinalizeResult::InvalidSource ||
        !fs::exists(futureFinalizeTemp) ||
        fs::exists(futureFinalize)) {

        std::cerr << "FAILED: FutureHour finalize protection\n";
        ok = false;
    }
    else {
        std::cout << "PASS: RejectFutureHourFinalize\n";
    }


    // ------------------------------------------------------------
    // Reject invalid source
    // ------------------------------------------------------------

    const std::string invalidFinalize =
        (dir / "finalize_invalid.hro").string();

    const std::string invalidFinalizeTemp =
        invalidFinalize + ".tmp";

    {
        std::ofstream file(invalidFinalizeTemp, std::ios::binary);
        file << "INVALID";
    }

    const auto invalidFinalizeResult =
        hro::finalizeOldObservationFile(
            invalidFinalizeTemp,
            "2026-09-27T08:00:00");

    if (invalidFinalizeResult != hro::FinalizeResult::InvalidSource ||
        !fs::exists(invalidFinalizeTemp) ||
        fs::exists(invalidFinalize)) {

        std::cerr << "FAILED: Invalid source finalize protection\n";
        ok = false;
    }
    else {
        std::cout << "PASS: RejectInvalidFinalize\n";
    }

    fs::remove_all(dir);

    return ok ? 0 : 1;
}
