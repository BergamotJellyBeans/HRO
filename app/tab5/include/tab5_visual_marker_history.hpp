#pragma once
#include "hro_plot.h"
#include <cstdint>
#include <limits>
namespace hro::tab5::app {
// Time-indexed bins, independent of sprite scrolling. One second = one pixel.
class VisualMarkerHistory {
public:
    static constexpr int HEIGHT = 4;
    static constexpr int MAX_HEIGHT = hro::plot::WATERFALL_HEIGHT;
    static int height(std::uint32_t count) {
        return static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(count) * HEIGHT, MAX_HEIGHT));
    }
    static constexpr int CAPACITY = hro::plot::SECONDS * 2 + 1; // Preserve the completed block during PNG work.
    void add(std::uint64_t received_ms, std::uint32_t count) {
        if (received_ms < 1577836800000ULL || !count) return;
        const auto second = static_cast<std::int64_t>(received_ms / 1000);
        auto& bin = bins_[second % CAPACITY];
        if (bin.second != second) { bin.second = second; bin.count = 0; }
        const auto sum = static_cast<std::uint64_t>(bin.count) + count;
        bin.count = static_cast<std::uint32_t>(std::min<std::uint64_t>(sum, UINT32_MAX));
    }
    std::uint32_t at(std::int64_t second) const {
        if (second <= 0) return 0;
        const auto& bin = bins_[second % CAPACITY];
        return bin.second == second ? bin.count : 0;
    }
    std::uint32_t column(int x, std::int64_t axis_end) const {
        if (x < 0 || x >= hro::plot::WIDTH) return 0;
        return at(axis_end - (hro::plot::WIDTH - 1 - x));
    }
private:
    struct Bin { std::int64_t second = 0; std::uint32_t count = 0; };
    Bin bins_[CAPACITY] = {};
};
}
