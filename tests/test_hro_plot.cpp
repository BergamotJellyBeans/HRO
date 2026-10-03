#include "hro_plot.h"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    using namespace hro::plot;
    static_assert(WIDTH == 1200 && SECONDS == 1200);
    assert(blockStart(1199) == 0 && blockStart(1200) == 1200);
    assert(blockStart(3600) == 3600 && blockStart(86399) == 85200);
    assert(binForRow(0) == 600 && binForRow(WATERFALL_HEIGHT - 1) == 0);
    for (int row = 0; row < WATERFALL_HEIGHT; ++row) {
        assert(binForRow(row) == static_cast<std::size_t>(std::lround(
            (1.0 - double(row) / (WATERFALL_HEIGHT - 1)) * (hro::FFT_BIN_COUNT - 1))));
    }
    assert(frequencyY(1080,780) == WATERFALL_TOP);
    assert(frequencyY(480,780) == WATERFALL_BOTTOM);
    assert(frequencyY(780,780) == 242.5);
    assert(LEFT_TICK_START == 43 && RIGHT_TICK_START == 1249);
    assert(AXIS_LABEL_RIGHT == 40);
    assert(latestTimeTick(120) == 120 && latestTimeTick(239) == 120);
    assert(latestTimeTick(240) == 240 && latestTimeTick(-1) == -120);
    assert(timeTickX(120,120) == RIGHT);
    assert(timeTickX(120,121) == RIGHT - 1);
    assert(timeTickX(120,239) == RIGHT - 119);
    assert(timeTickX(120,240) == RIGHT - 120);
    assert(timeTickX(120,1320) == LEFT);
    assert(levelY(-10) == 79 && levelY(30) == 0);
    assert(levelY(-100) == 79 && levelY(100) == 0);
    auto low = waterfallColor(-20); assert(low.r == 0 && low.g == 0 && low.b == 30);
    auto high = waterfallColor(30); assert(high.r == 255 && high.g == 255 && high.b == 255);
    assert(waterfallColor(std::numeric_limits<double>::quiet_NaN()).b == 30);
}
