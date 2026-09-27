#include "dsp/fs4_rotator.h"

#include <cassert>
#include <complex>
#include <iostream>

int main()
{
    using hro::dsp::Fs4Rotator;
    using C = std::complex<float>;

    Fs4Rotator rotator;

    // Constant complex input 1 + j0.
    // Multiplying by exp(-j*pi*n/2) should produce:
    //
    //   1, -j, -1, +j, ...
    //
    C block1[] = {
        {1.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 0.0f}
    };

    rotator.process(block1, 3);

    assert(block1[0] == C( 1.0f,  0.0f));
    assert(block1[1] == C( 0.0f, -1.0f));
    assert(block1[2] == C(-1.0f,  0.0f));

    // Important:
    // phase must continue from the previous block.
    C block2[] = {
        {1.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 0.0f}
    };

    rotator.process(block2, 3);

    assert(block2[0] == C( 0.0f,  1.0f));
    assert(block2[1] == C( 1.0f,  0.0f));
    assert(block2[2] == C( 0.0f, -1.0f));

    // reset() must restart the sequence at phase zero.
    rotator.reset();

    C sample = {1.0f, 0.0f};
    rotator.process(&sample, 1);

    assert(sample == C(1.0f, 0.0f));

    std::cout << "Fs4Rotator test passed\n";
    return 0;
}
