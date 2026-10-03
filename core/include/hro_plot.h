#pragma once
#include "hro_fft_config.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Pure observation/plot specification shared by Cairo and the Tab5 LCD.
namespace hro::plot {
inline constexpr int IMAGE_WIDTH = 1280;
inline constexpr int IMAGE_HEIGHT = 480;
inline constexpr int STATION_GAIN_X = 1050;
inline constexpr int SECONDS = 20 * 60;
inline constexpr int TIME_TICK_SECONDS = 120;
inline constexpr std::size_t FFT_SIZE = 8192;
inline constexpr int LEFT = 48;
inline constexpr int RIGHT = 1248;
inline constexpr int WIDTH = RIGHT - LEFT;
inline constexpr int WATERFALL_TOP = 125;
inline constexpr int WATERFALL_BOTTOM = 360;
inline constexpr int WATERFALL_HEIGHT = WATERFALL_BOTTOM - WATERFALL_TOP;
// Axis geometry is shared; each platform chooses its drawing API and font.
inline constexpr int FREQUENCY_TICK_HZ = 100;
inline constexpr int AXIS_LABEL_RIGHT = LEFT - 8;
inline constexpr int LEFT_TICK_START = LEFT - 5;
inline constexpr int LEFT_TICK_END = LEFT - 1;
inline constexpr int RIGHT_TICK_START = RIGHT + 1;
inline constexpr int RIGHT_TICK_END = RIGHT + 5;
inline constexpr int LEVEL_TOP = 370;
inline constexpr int LEVEL_HEIGHT = 80;
inline constexpr double LEVEL_DB_MIN = -10.0;
inline constexpr double LEVEL_DB_MAX = 30.0;
static_assert(WIDTH == SECONDS, "One second must occupy one pixel");
static_assert(LEVEL_TOP + LEVEL_HEIGHT <= IMAGE_HEIGHT, "Plot must fit image");

inline double frequencyY(int frequencyHz, int centerHz) {
    const double ratio = (static_cast<double>(centerHz) + hro::FFT_RANGE_HZ - frequencyHz)
        / (2.0 * hro::FFT_RANGE_HZ);
    return WATERFALL_TOP + WATERFALL_HEIGHT * ratio;
}

// Tick times stay on absolute two-minute boundaries while their X positions move.
inline std::int64_t latestTimeTick(std::int64_t seconds) {
    const auto remainder = seconds % TIME_TICK_SECONDS;
    return seconds - (remainder < 0 ? remainder + TIME_TICK_SECONDS : remainder);
}
inline int timeTickX(std::int64_t tickTime, std::int64_t axisEnd) {
    return RIGHT - static_cast<int>((axisEnd - tickTime) * WIDTH / SECONDS);
}

inline std::int64_t blockStart(std::int64_t seconds) {
    return seconds - seconds % SECONDS;
}
inline std::size_t binForRow(int row) {
    row = std::clamp(row, 0, WATERFALL_HEIGHT - 1);
    const int denominator = WATERFALL_HEIGHT - 1;
    return ((denominator - row) * (hro::FFT_BIN_COUNT - 1) + denominator / 2) / denominator;
}
inline int levelY(double db) {
    if (!std::isfinite(db)) db = LEVEL_DB_MIN;
    db = std::clamp(db, LEVEL_DB_MIN, LEVEL_DB_MAX);
    return LEVEL_HEIGHT - 1 - static_cast<int>(std::floor(
        (db - LEVEL_DB_MIN) / (LEVEL_DB_MAX - LEVEL_DB_MIN) * (LEVEL_HEIGHT - 1)));
}
struct Rgb { double r, g, b; };
inline Rgb waterfallColor(double db) {
    if (!std::isfinite(db)) db = -20.0;
    const double t = std::clamp((db + 20.0) / 50.0, 0.0, 1.0);
    if (t < 0.25) return {0.0, 0.0, 30.0 + 180.0 * (t / 0.25)};
    if (t < 0.50) return {0.0, 220.0 * ((t - 0.25) / 0.25), 220.0};
    if (t < 0.75) { const double u = (t - 0.50) / 0.25;
        return {255.0 * u, 220.0, 220.0 * (1.0 - u)}; }
    const double u = (t - 0.75) / 0.25;
    return {255.0, 220.0 + 35.0 * u, 255.0 * u};
}
} // namespace hro::plot
