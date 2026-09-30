#pragma once

#include <cstdint>
#include <vector>

struct rtlsdr_dev;

class RtlSdrSource
{
public:
    RtlSdrSource();
    ~RtlSdrSource();

    RtlSdrSource(const RtlSdrSource&) = delete;
    RtlSdrSource& operator=(const RtlSdrSource&) = delete;

    bool open(uint32_t deviceIndex = 0);
    void close();

    bool setSampleRate(uint32_t sampleRate);
    bool setCenterFrequency(uint32_t frequencyHz);
    bool setTunerGain(int gainTenthsDb);
    bool resetBuffer();

    // Reads raw RTL-SDR interleaved unsigned 8-bit IQ:
    // I0, Q0, I1, Q1, ...
    bool read(std::vector<uint8_t>& buffer, int& bytesRead);

private:
    rtlsdr_dev* device_ = nullptr;
};
