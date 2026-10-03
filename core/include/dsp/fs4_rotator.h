#pragma once

#include <cstddef>
#include <complex>

namespace hro::dsp {

/**
 * Shift complex IQ by +Fs/4.
 *
 * Multiplication sequence:
 *
 *   1, +j, -1, -j, ...
 *
 * No sin/cos calculation is required.
 *
 * phase must be preserved between consecutive blocks.
 */
class Fs4Rotator
{
public:
    void reset();

    void process(std::complex<float>* samples, std::size_t count);

private:
    unsigned phase_ = 0;
};

} // namespace hro::dsp
