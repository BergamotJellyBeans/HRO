#include "hro_header.h"
#include "hro_writer.h"

#include <array>
#include <filesystem>
#include <iomanip>
#include <sstream>

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
    // Header       : SPACE filled
    // Validity Map : zero filled
    // Event Map    : zero filled
    // FFT Data     : zero filled

    std::array<char, HEADER_SIZE> header{};
    header.fill(' ');

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

    headerCardCount_ = 0;
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

    // Helper for checking exact 80-byte header cards.
 HroHeader parsedHeader;

    if (!parsedHeader.load(header)) {
        file_.close();
        return false;
    }

    if (!parsedHeader.isValidV1()) {
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
    if (!open_ || headerFinished_) {
        return false;
    }

    auto write = [this](const std::string& key,
                        const std::string& value) -> bool
    {
        return writeHeaderCard(key + "=" + value);
    };

    auto writeUInt = [&write](const std::string& key,
                              auto value) -> bool
    {
        return write(key, std::to_string(value));
    };

    auto writeDouble = [&write](const std::string& key,
                                double value,
                                int precision) -> bool
    {
        std::ostringstream ss;
        ss << std::fixed
           << std::setprecision(precision)
           << value;

       std::string text = ss.str();

        // Remove unnecessary trailing zeros.
        const auto dot = text.find('.');

        if (dot != std::string::npos) {
            while (text.size() > dot + 2 &&
               text.back() == '0') {
                text.pop_back();
            }
        }

        return write(key, text);
    };


    // -------------------------------------------------------------------------
    // File format
    // -------------------------------------------------------------------------

    if (!writeUInt("HRO_FILE_VERSION", FILE_VERSION)) return false;
    if (!writeUInt("HEADER_SIZE", HEADER_SIZE)) return false;
    if (!writeUInt("VALIDITY_MAP_SIZE", VALIDITY_MAP_SIZE)) return false;

    if (!write("DATA_TYPE", "FLOAT32")) return false;
    if (!write("BYTE_ORDER", "LITTLE_ENDIAN")) return false;


    // -------------------------------------------------------------------------
    // Time
    // -------------------------------------------------------------------------

    if (!write("FILE_START_UTC", metadata.fileStartUtc)) return false;
    if (!write("FILE_HOUR_LOCAL", metadata.fileHourLocal)) return false;
    if (!write("TIME_ZONE", metadata.timeZone)) return false;
    if (!write("START_TIME_SOURCE", metadata.startTimeSource)) return false;

    if (!writeUInt("NTP_SYNCED", metadata.ntpSynced ? 1 : 0)) return false;
    if (!write("NTP_SYNC_TIME_UTC", metadata.ntpSyncTimeUtc)) return false;
    if (!writeUInt("TIME_CONFLICTS", metadata.timeConflicts)) return false;


    // -------------------------------------------------------------------------
    // Observer / location
    // -------------------------------------------------------------------------

    if (!write("OBSERVER", metadata.observer)) return false;
    if (!write("LOCATION", metadata.location)) return false;

    if (!writeDouble("LONGITUDE", metadata.longitude, 6)) return false;
    if (!writeDouble("LATITUDE", metadata.latitude, 6)) return false;


    // -------------------------------------------------------------------------
    // Receiver
    // -------------------------------------------------------------------------

    if (!write("RECEIVER", metadata.receiver)) return false;
    if (!write("ANTENNA", metadata.antenna)) return false;

    if (!writeUInt("RF_FREQUENCY_HZ",
                   metadata.rfFrequencyHz)) return false;

    if (!writeUInt("SDR_LO_FREQUENCY_HZ",
                   metadata.sdrLoFrequencyHz)) return false;

    if (!writeUInt("SDR_SAMPLE_RATE_HZ",
                   metadata.sdrSampleRateHz)) return false;


    // -------------------------------------------------------------------------
    // FFT
    // -------------------------------------------------------------------------

    if (!writeUInt("FFT_SIZE", FFT_SIZE)) return false;

    if (!writeUInt("FFT_OUTPUT_RATE_HZ",
                   metadata.fftOutputRateHz)) return false;

    if (!writeDouble("FFT_RESOLUTION_HZ",
                     metadata.fftResolutionHz, 6)) return false;

    if (!writeDouble("FFT_CENTER_HZ",
                     metadata.fftCenterHz, 3)) return false;

    if (!writeDouble("FFT_RANGE_HZ",
                     metadata.fftRangeHz, 3)) return false;

    if (!writeUInt("FFT_MIN_HZ", BIN_MIN)) return false;
    if (!writeUInt("FFT_MAX_HZ", BIN_MAX)) return false;
    if (!writeUInt("FFT_BIN_COUNT", BIN_COUNT)) return false;


    // -------------------------------------------------------------------------
    // Record layout
    // -------------------------------------------------------------------------

    if (!writeUInt("RECORD_INTERVAL_SEC",
                   RECORD_INTERVAL_SEC)) return false;

    if (!writeUInt("RECORD_SLOTS",
                   RECORD_SLOTS)) return false;


    // -------------------------------------------------------------------------
    // Event map
    // -------------------------------------------------------------------------

    if (!writeUInt("EVENT_MAP_SIZE",
                   EVENT_MAP_SIZE)) return false;

    if (!write("EVENT_TYPE",
               "UINT8_BIT_FLAGS")) return false;


    // -------------------------------------------------------------------------
    // Device / software
    // -------------------------------------------------------------------------

    if (!write("DEVICE", metadata.device)) return false;
    if (!write("SOFTWARE", metadata.software)) return false;
    if (!write("SOFTWARE_VERSION",
               metadata.softwareVersion)) return false;


    return finishHeader();
}

bool HroWriter::writeHeaderCard(const std::string& card)
{
    if (!open_ || headerFinished_) {
        return false;
    }

    // Header has room for exactly 100 cards.
    if (headerCardCount_ >= HEADER_CARD_COUNT) {
        return false;
    }

    // One card must fit within 80 bytes.
    if (card.size() > HEADER_CARD_SIZE) {
        return false;
    }

    std::array<char, HEADER_CARD_SIZE> buffer{};
    buffer.fill(' ');

    std::copy(card.begin(), card.end(), buffer.begin());

    const std::streamoff offset =
        static_cast<std::streamoff>(
            headerCardCount_ * HEADER_CARD_SIZE
        );

    file_.seekp(offset);

    if (!file_) {
        return false;
    }

    file_.write(
        buffer.data(),
        static_cast<std::streamsize>(buffer.size())
    );

    if (!file_) {
        return false;
    }

    ++headerCardCount_;

    return true;
}

bool HroWriter::isOpen() const
{
    return open_;
}

bool HroWriter::finishHeader()
{
    if (!open_ || headerFinished_) {
        return false;
    }

    // Reserve one card for END_HEADER.
    if (headerCardCount_ >= HEADER_CARD_COUNT) {
        return false;
    }

    if (!writeHeaderCard("END_HEADER")) {
        return false;
    }

    // Explicitly initialize the Validity Map to zero.
    std::array<char, VALIDITY_MAP_SIZE> validity{};
    validity.fill(0);

    file_.seekp(
        static_cast<std::streamoff>(VALIDITY_MAP_OFFSET)
    );

    if (!file_) {
        return false;
    }

    file_.write(
        validity.data(),
        static_cast<std::streamsize>(validity.size())
    );

    if (!file_) {
        return false;
    }

    // Explicitly initialize the Event Map to zero.
    std::array<char, EVENT_MAP_SIZE> events{};
    events.fill(0);

    file_.seekp(
        static_cast<std::streamoff>(EVENT_MAP_OFFSET)
    );

    if (!file_) {
        return false;
    }

    file_.write(
        events.data(),
        static_cast<std::streamsize>(events.size())
    );

    if (!file_) {
        return false;
    }

    file_.flush();

    if (!file_) {
        return false;
    }

    headerFinished_ = true;

    return true;
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

    file_.write(
        reinterpret_cast<const char*>(spectrum),
        static_cast<std::streamsize>(SPECTRUM_RECORD_SIZE)
    );

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

    if (slot >= RECORD_SLOTS) {
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
    headerCardCount_ = 0;

    return true;
}

} // namespace hro
