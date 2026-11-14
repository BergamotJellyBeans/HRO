#pragma once
#include "hro_config.h"
#include "hro_format.h"
#include <array>
#include <filesystem>
#include <fstream>
namespace hro::pi {
// Single writer, called synchronously by the FFT producer before UDP transmission.
class HourlyHroRecorder {
public:
    HourlyHroRecorder(std::filesystem::path directory, HroConfig config);
    ~HourlyHroRecorder();
    bool record(std::int64_t timestampMs, std::uint64_t sequence,
                const std::array<float, BIN_COUNT>& bins);
    bool stop();
private:
    bool begin(std::int64_t hour);
    std::filesystem::path directory_, finalPath_, tempPath_;
    HroConfig config_;
    std::ofstream file_;
    std::int64_t hour_ = -1;
};
}
