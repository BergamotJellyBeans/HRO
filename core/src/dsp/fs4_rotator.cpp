#include "dsp/fs4_rotator.h"

namespace hro::dsp {

void Fs4Rotator::reset()
{
    phase_ = 0;
}

void Fs4Rotator::process(std::complex<float>* samples, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i) {
        const float re = samples[i].real();
        const float im = samples[i].imag();

        // +Fs/4 rotation
        switch (phase_) {
        case 0:
            // × 1
            break;

        case 1:
            // × +j
            samples[i] = {-im, re};
            break;

        case 2:
            // × -1
            samples[i] = {-re, -im};
            break;

        case 3:
            // × -j
            samples[i] = {im, -re};
            break;
        }
        phase_ = (phase_ + 1) & 3;
    }
}

} // namespace hro::dsp
