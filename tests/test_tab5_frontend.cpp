#include "tab5_frontend.h"
#include <cassert>
#include <cmath>
#include <complex>
int main() {
    hro::tab5::Frontend frontend;
    std::complex<float> output;
    unsigned count = 0;
    double error = 0;
    // -Fs/4 -> DC after Tab5's +Fs/4 rotation. /4 must produce exactly 64 kS/s.
    for (unsigned n = 0; n < hro::tab5::INPUT_RATE; ++n) {
        std::complex<float> input;
        switch (n & 3) {
        case 0: input = {1,0}; break;
        case 1: input = {0,-1}; break;
        case 2: input = {-1,0}; break;
        default: input = {0,1}; break;
        }
        if (frontend.process(input.real(), input.imag(), output)) {
            ++count;
            if (count > 127) error += std::abs(output - std::complex<float>(1,0));
        }
    }
    assert(count == hro::tab5::OUTPUT_RATE);
    assert(error / (count - 127) < 1e-4);
}
