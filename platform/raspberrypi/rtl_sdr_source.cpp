#include "rtl_sdr_source.h"

#include <rtl-sdr.h>

RtlSdrSource::RtlSdrSource() = default;

RtlSdrSource::~RtlSdrSource()
{
    close();
}

bool RtlSdrSource::open(uint32_t deviceIndex)
{
    if (device_ != nullptr) {
        return true;
    }

    return rtlsdr_open(&device_, deviceIndex) == 0;
}

void RtlSdrSource::close()
{
    if (device_ != nullptr) {
        rtlsdr_close(device_);
        device_ = nullptr;
    }
}

bool RtlSdrSource::setSampleRate(uint32_t sampleRate)
{
    if (device_ == nullptr) {
        return false;
    }

    return rtlsdr_set_sample_rate(device_, sampleRate) == 0;
}

bool RtlSdrSource::setCenterFrequency(uint32_t frequencyHz)
{
    if (device_ == nullptr) {
        return false;
    }

    return rtlsdr_set_center_freq(device_, frequencyHz) == 0;
}

bool RtlSdrSource::resetBuffer()
{
    if (device_ == nullptr) {
        return false;
    }

    return rtlsdr_reset_buffer(device_) == 0;
}

bool RtlSdrSource::read(std::vector<uint8_t>& buffer, int& bytesRead)
{
    bytesRead = 0;

    if (device_ == nullptr || buffer.empty()) {
        return false;
    }

    return rtlsdr_read_sync(
               device_,
               buffer.data(),
               static_cast<int>(buffer.size()),
               &bytesRead) == 0;
}
