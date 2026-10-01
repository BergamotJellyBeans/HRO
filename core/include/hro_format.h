#pragma once

#include "hro_fft_config.h"

#include <cstddef>
#include <cstdint>

namespace hro {

// =============================================================================
// HRO File Format Version 1
// =============================================================================
//
// File layout:
//
//   +-----------------------------+  offset 0
//   | Header                      |  8192 bytes
//   +-----------------------------+  offset 8192
//   | Validity Map                |   512 bytes
//   +-----------------------------+  offset 8704
//   | Event Map                   |  3600 bytes
//   +-----------------------------+  offset 12304
//   | Spectrum Data               |
//   | 3600 x 601 x float32        |    501->601
//   +-----------------------------+
//
// One .hro file represents one clock hour.
// One spectrum record is stored for each one-second slot.
//
// =============================================================================


// -----------------------------------------------------------------------------
// File format
// -----------------------------------------------------------------------------

constexpr std::uint32_t FILE_VERSION = 1;


// -----------------------------------------------------------------------------
// Header
// -----------------------------------------------------------------------------
//
// Header is inspired by FITS.
//
// Each header card:
//   - exactly 80 bytes
//   - ASCII text
//   - padded with ASCII SPACE (0x20)
//   - no newline
//   - no NUL terminator
//
// 100 cards occupy 8000 bytes.
// Remaining 192 bytes are reserved and SPACE-filled.
//
// Logical end of header is indicated by:
//
//   END_HEADER
//
// -----------------------------------------------------------------------------

constexpr std::size_t HEADER_SIZE       = 8192;
constexpr std::size_t HEADER_CARD_SIZE  = 80;
constexpr std::size_t HEADER_CARD_COUNT = 100;

constexpr std::size_t HEADER_CARD_AREA_SIZE =
    HEADER_CARD_SIZE * HEADER_CARD_COUNT;

constexpr std::size_t HEADER_PADDING_SIZE =
    HEADER_SIZE - HEADER_CARD_AREA_SIZE;


// -----------------------------------------------------------------------------
// Observation slots
// -----------------------------------------------------------------------------

constexpr std::size_t RECORD_SLOTS = 3600;
constexpr std::size_t RECORD_INTERVAL_SEC = 1;


// -----------------------------------------------------------------------------
// Validity Map
// -----------------------------------------------------------------------------
//
// One bit represents one one-second slot.
//
// bit = 1 : valid spectrum data exists
// bit = 0 : missing / invalid data
//
// Bit order is LSB-first:
//
//   second 0 -> byte[0] bit 0 (0x01)
//   second 1 -> byte[0] bit 1 (0x02)
//   ...
//   second 7 -> byte[0] bit 7 (0x80)
//   second 8 -> byte[1] bit 0
//
// 3600 bits = 450 bytes are used.
// The remaining bytes are reserved.
//
// -----------------------------------------------------------------------------

constexpr std::size_t VALIDITY_MAP_SIZE = 512;

constexpr std::size_t VALIDITY_USED_BYTES =
    (RECORD_SLOTS + 7) / 8;

constexpr std::size_t validityByteIndex(std::size_t slot)
{
    return slot / 8;
}

constexpr std::uint8_t validityBitMask(std::size_t slot)
{
    return static_cast<std::uint8_t>(1u << (slot % 8));
}


// -----------------------------------------------------------------------------
// Event Map
// -----------------------------------------------------------------------------
//
// One byte represents one one-second slot.
//
// Event values are bit flags, allowing multiple events in the same second.
//
// Version 1:
//
//   bit 0 (0x01) : VISUAL meteor observation
//   bits 1..7    : reserved
//
// -----------------------------------------------------------------------------

using EventValue = std::uint8_t;

constexpr std::size_t EVENT_MAP_SIZE = RECORD_SLOTS;

constexpr EventValue EVENT_NONE   = 0x00;
constexpr EventValue EVENT_VISUAL = 0x01;


// -----------------------------------------------------------------------------
// FFT spectrum
// -----------------------------------------------------------------------------

constexpr std::size_t FFT_SIZE = 8192;

constexpr int BIN_MIN = 480;    // 530->480
constexpr int BIN_MAX = 1080;   // 1030->1080

constexpr std::size_t BIN_COUNT =
    static_cast<std::size_t>(BIN_MAX - BIN_MIN + 1);

// Stored as IEEE-754 float32.
using SpectrumValue = float;

// One second of FFT spectrum data.
struct SpectrumRecord
{
    SpectrumValue bins[BIN_COUNT];
};

constexpr std::size_t SPECTRUM_RECORD_SIZE =
    BIN_COUNT * sizeof(SpectrumValue);


// -----------------------------------------------------------------------------
// File offsets
// -----------------------------------------------------------------------------

constexpr std::size_t VALIDITY_MAP_OFFSET =
    HEADER_SIZE;

constexpr std::size_t EVENT_MAP_OFFSET =
    VALIDITY_MAP_OFFSET + VALIDITY_MAP_SIZE;

constexpr std::size_t DATA_OFFSET =
    EVENT_MAP_OFFSET + EVENT_MAP_SIZE;


// -----------------------------------------------------------------------------
// File size
// -----------------------------------------------------------------------------

constexpr std::size_t DATA_SIZE =
    RECORD_SLOTS * SPECTRUM_RECORD_SIZE;

constexpr std::size_t FILE_SIZE =
    DATA_OFFSET + DATA_SIZE;


// -----------------------------------------------------------------------------
// Offset helpers
// -----------------------------------------------------------------------------

constexpr std::size_t eventOffset(std::size_t slot)
{
    return EVENT_MAP_OFFSET + slot;
}

constexpr std::size_t spectrumOffset(std::size_t slot)
{
    return DATA_OFFSET + slot * SPECTRUM_RECORD_SIZE;
}


// -----------------------------------------------------------------------------
// Compile-time format checks
// -----------------------------------------------------------------------------

static_assert(HEADER_CARD_AREA_SIZE == 8000,
              "Unexpected HRO header card area size");

static_assert(HEADER_PADDING_SIZE == 192,
              "Unexpected HRO header padding size");

static_assert(RECORD_SLOTS == 3600,
              "HRO file must contain 3600 one-second slots");

static_assert(VALIDITY_USED_BYTES == 450,
              "Unexpected HRO validity map usage");

static_assert(BIN_COUNT == hro::FFT_BIN_COUNT,
              "HRO spectrum bin count must match FFT configuration");

static_assert(sizeof(SpectrumValue) == 4,
              "HRO spectrum value must be 32-bit float");

static_assert(sizeof(SpectrumRecord) == SPECTRUM_RECORD_SIZE,
              "Unexpected SpectrumRecord layout");

static_assert(SPECTRUM_RECORD_SIZE == 2404, // 2004->2404
              "Unexpected HRO spectrum record size");

static_assert(VALIDITY_MAP_OFFSET == 8192,
              "Unexpected validity map offset");

static_assert(EVENT_MAP_OFFSET == 8704,
              "Unexpected event map offset");

static_assert(DATA_OFFSET == 12304,
              "Unexpected spectrum data offset");

static_assert(DATA_SIZE == 8654400, // 7214400->8654400
              "Unexpected HRO spectrum data size");

static_assert(FILE_SIZE == 8666704, // 7226704->8666704
              "Unexpected HRO file size");

} // namespace hro