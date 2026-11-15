#include "hro_header.h"
#include "hro_writer.h"

#include <array>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cmath>

namespace hro {

HroWriter::~HroWriter()
{
    if (file_.is_open()) {
        file_.close();
    }
}

bool HroWriter::open(const std::string& finalPath)
{
    if (open_) {
        return false;
    }
    // Never truncate an existing final or temporary observation.
    if (std::filesystem::exists(finalPath) || std::filesystem::exists(finalPath + ".tmp")) return false;
    file_.clear();

    finalPath_ = finalPath;
    tempPath_ = finalPath + ".tmp";

    file_.open(
        tempPath_,
        std::ios::binary |
        std::ios::in |
        std::ios::out |
        std::ios::trunc
    );

    if (!file_.is_open()) {
        return false;
    }

    // Pre-allocate the complete HRO file layout.
    //
    // Header       : zero filled
    // Validity Map : zero filled
    // Event Map    : zero filled
    // FFT Data     : zero filled

    std::array<char, HEADER_SIZE> header{};


    file_.write(header.data(),
                static_cast<std::streamsize>(header.size()));

    if (!file_) {
        file_.close();
        return false;
    }

    // Extend the file to its final fixed size.
    file_.seekp(static_cast<std::streamoff>(FILE_SIZE - 1));

    const char zero = 0;
    file_.write(&zero, 1);

    if (!file_) {
        file_.close();
        return false;
    }

    file_.flush();

    headerFinished_ = false;
    open_ = true;

    return true;
}

bool HroWriter::resume(const std::string& tempPath)
{
    if (open_) {
        return false;
    }

    // Resume is only for an existing temporary HRO file.
    if (tempPath.size() < 4 ||
        tempPath.substr(tempPath.size() - 4) != ".tmp") {
        return false;
    }

    std::error_code ec;

    const auto size = std::filesystem::file_size(tempPath, ec);

    if (ec || size != FILE_SIZE) {
        return false;
    }

    file_.clear();
    file_.open(
        tempPath,
        std::ios::binary |
        std::ios::in |
        std::ios::out
    );

    if (!file_.is_open()) {
        return false;
    }

    // Read the complete fixed-size header.
    std::array<char, HEADER_SIZE> header{};

    file_.seekg(0);

    file_.read(
        header.data(),
        static_cast<std::streamsize>(header.size())
    );

    if (!file_) {
        file_.close();
        return false;
    }

    HroHeader parsedHeader;

    if (!parsedHeader.load(header)) {
        file_.close();
        return false;
    }

    std::string layout;
    if (!parsedHeader.isValidV2() || parsedHeader.getValue("RECORD_LAYOUT", layout)) {
        file_.close();
        return false;
    }

    tempPath_ = tempPath;

    // Remove ".tmp" to obtain the final .hro path.
    finalPath_ =
        tempPath.substr(0, tempPath.size() - 4);

    headerFinished_ = true;
    open_ = true;

    return true;
}

bool HroWriter::writeHeader(const HroMetadata& metadata)
{
    if (!open_ || headerFinished_) return false;
    std::array<char, HEADER_SIZE> header{};
    if (!HroHeader::encode(metadata, header)) return false;
    file_.seekp(0);
    file_.write(header.data(), header.size());
    file_.flush();
    headerFinished_ = static_cast<bool>(file_);
    return headerFinished_;
}

bool HroWriter::isOpen() const { return open_; }
bool HroWriter::flush() { file_.flush(); return open_ && static_cast<bool>(file_); }
bool HroWriter::noteTimeConflict()
{
    if (!open_ || !headerFinished_) return false;
    std::array<char, HEADER_SIZE> bytes{};
    file_.seekg(0); file_.read(bytes.data(), bytes.size());
    if (!file_ || !HroHeader::incrementTimeConflicts(bytes)) return false;
    file_.seekp(0); file_.write(bytes.data(), bytes.size());
    return flush();
}
bool HroWriter::hasSpectrum(std::size_t slot)
{
    if (!open_ || !headerFinished_ || slot >= RECORD_SLOTS) return false;
    file_.seekg(VALIDITY_MAP_OFFSET + validityByteIndex(slot));
    unsigned char value = 0;
    file_.read(reinterpret_cast<char*>(&value), 1);
    return file_ && (value & validityBitMask(slot));
}

bool HroWriter::setValidity(std::size_t slot)
{
    if (!open_ || !headerFinished_) {
        return false;
    }

    if (slot >= RECORD_SLOTS) {
        return false;
    }

    const std::size_t byteIndex = validityByteIndex(slot);
    const std::uint8_t mask = validityBitMask(slot);

    const std::streamoff offset =
        static_cast<std::streamoff>(
            VALIDITY_MAP_OFFSET + byteIndex
        );

    file_.seekg(offset);

    if (!file_) {
        return false;
    }

    std::uint8_t value = 0;

    file_.read(
        reinterpret_cast<char*>(&value),
        sizeof(value)
    );

    if (!file_) {
        return false;
    }

    value |= mask;

    // Switching from read to write: seek to the target position again.
    file_.seekp(offset);

    if (!file_) {
        return false;
    }

    file_.write(
        reinterpret_cast<const char*>(&value),
        sizeof(value)
    );

    return static_cast<bool>(file_);
}

bool HroWriter::writeSpectrum(
    std::size_t slot,
    const SpectrumValue* spectrum)
{
    if (!open_ || !headerFinished_) {
        return false;
    }

    if (slot >= RECORD_SLOTS || spectrum == nullptr) {
        return false;
    }

    const std::streamoff offset =
        static_cast<std::streamoff>(
            spectrumOffset(slot)
        );

    file_.seekp(offset);

    if (!file_) {
        return false;
    }

    // Explicit IEEE-754 little endian encoding, independent of host byte order.
    std::array<char, SPECTRUM_RECORD_SIZE> bytes{};
    for (std::size_t i = 0; i < BIN_COUNT; ++i) {
        if (!std::isfinite(spectrum[i])) return false;
        std::uint32_t bits; std::memcpy(&bits, &spectrum[i], 4);
        for (unsigned k = 0; k < 4; ++k) bytes[i * 4 + k] = static_cast<char>(bits >> (k * 8));
    }
    file_.write(bytes.data(), bytes.size());

    if (!file_) {
        return false;
    }

    // Mark this slot valid only after the spectrum write succeeded.
    if (!setValidity(slot)) {
        return false;
    }

    return true;
}

bool HroWriter::setEvent(
    std::size_t slot,
    EventValue event)
{
    if (!open_ || !headerFinished_) {
        return false;
    }

    if (slot >= RECORD_SLOTS || (event & ~EVENT_VISUAL)) {
        return false;
    }

    const std::streamoff offset =
        static_cast<std::streamoff>(
            eventOffset(slot)
        );

    file_.seekg(offset);

    if (!file_) {
        return false;
    }

    EventValue value = EVENT_NONE;

    file_.read(
        reinterpret_cast<char*>(&value),
        sizeof(value)
    );

    if (!file_) {
        return false;
    }

    // Preserve existing event flags.
    value |= event;

    file_.seekp(offset);

    if (!file_) {
        return false;
    }

    file_.write(
        reinterpret_cast<const char*>(&value),
        sizeof(value)
    );

    return static_cast<bool>(file_);
}

bool HroWriter::close()
{
    if (!open_) {
        return false;
    }

    if (!headerFinished_) {
        return false;
    }

    file_.flush();

    if (!file_) {
        return false;
    }

    file_.close();

    if (file_.fail()) {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(finalPath_, ec) || ec) return false;

    std::filesystem::rename(
        tempPath_,
        finalPath_,
        ec
    );

    if (ec) {
        return false;
    }

    open_ = false;
    headerFinished_ = false;

    return true;
}

} // namespace hro
