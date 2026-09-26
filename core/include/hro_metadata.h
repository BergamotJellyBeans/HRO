#pragma once

#include <cstdint>
#include <string>

namespace hro {

// Observation and acquisition metadata stored in the HRO header.
//
// File-format constants such as HEADER_SIZE, FFT_SIZE and BIN_COUNT
// are defined by hro_format.h and are not duplicated here.
struct HroMetadata
{
    // -------------------------------------------------------------------------
    // Time
    // -------------------------------------------------------------------------

    std::string fileStartUtc;
    std::string fileHourLocal;
    std::string timeZone;

    std::string startTimeSource;

    bool ntpSynced = false;
    std::string ntpSyncTimeUtc;

    std::uint32_t timeConflicts = 0;


    // -------------------------------------------------------------------------
    // Observer / location
    // -------------------------------------------------------------------------

    std::string observer;
    std::string location;

    double longitude = 0.0;
    double latitude  = 0.0;


    // -------------------------------------------------------------------------
    // Receiver / antenna
    // -------------------------------------------------------------------------

    std::string receiver;
    std::string antenna;

    std::uint64_t rfFrequencyHz    = 0;
    std::uint64_t sdrLoFrequencyHz = 0;
    std::uint32_t sdrSampleRateHz  = 0;


    // -------------------------------------------------------------------------
    // FFT observation conditions
    // -------------------------------------------------------------------------

    std::uint32_t fftOutputRateHz = 8192;

    double fftResolutionHz = 1.0;
    double fftCenterHz     = 780.0;
    double fftRangeHz      = 250.0;


    // -------------------------------------------------------------------------
    // Device / software
    // -------------------------------------------------------------------------

    std::string device;
    std::string software;
    std::string softwareVersion;
};

} // namespace hro
