# HRO File Format Specification — Third Edition

[日本語](HRO_File_Format_Specification.md) | English

Document revision: Third Edition (2026-10-04)  
File format: HRO File Version 2

The first edition's 501 bins per second (780 Hz center, ±250 Hz) have been
updated to 601 bins per second (configured center frequency, ±300 Hz).
780 Hz is an example setting, not a fixed value. The center frequency is
configurable. The layout and starting offsets of the Header, Validity Map,
and Event Map remain unchanged.

## 1. Overview

The HRO File Format is a common format for storing observation data from
HRO (Ham-band Radio Observation) meteor radio observations.

It is intended for use by Raspberry Pi observation instruments and future
viewer and analysis software.

In the current operational arrangement, Pi5 stores HRO data. Tab5 stores
PNG images only and does not write or read HRO data. The format itself is
device-independent.

Version 2 stores one hour of observation data in each file.

A file consists of four regions:

1. JSON Header
2. Validity Map
3. Event Map
4. FFT Data

## 2. Basic Specifications

| Item | Value |
| --- | --- |
| HRO File Version | 2 |
| Observation duration per file | 3600 seconds (1 hour) |
| Header Size | 8192 bytes |
| Header Encoding | UTF-8 |
| Header Padding | 0x00 |
| JSON Maximum Size | 8191 bytes |
| Validity Map Size | 512 bytes |
| Event Map Size | 3600 bytes |
| FFT Data Type | IEEE-754 FLOAT32 |
| Byte Order | LITTLE_ENDIAN |
| Record Interval | 1 second |
| Record Slots | 3600 |

## 3. File Layout

```text
Offset 0
+--------------------------------------+
| JSON Header                          |
| 8192 bytes                           |
| UTF-8 JSON (maximum 8191 bytes)       |
| Remaining bytes padded with 0x00      |
+--------------------------------------+
Offset 8192
| Validity Map                         |
| 512 bytes                            |
+--------------------------------------+
Offset 8704
| Event Map                            |
| 3600 bytes                           |
+--------------------------------------+
Offset 12304
| FFT Data                             |
| 2404 bytes × 3600 records             |
+--------------------------------------+
```

The third edition's standard parameters produce 601 bins per second,
regardless of the center frequency.

```text
FFT_BIN_COUNT = 601
sizeof(FLOAT32) = 4 bytes

FFT_RECORD_SIZE
    = 601 × 4
    = 2404 bytes

FFT_DATA_SIZE
    = 2404 × 3600
    = 8,654,400 bytes

FILE_SIZE
    = 8192 + 512 + 3600 + 8,654,400
    = 8,666,704 bytes
```

The starting offset of each region is calculated from the region sizes
recorded in the Header.

```text
VALIDITY_MAP_OFFSET = HEADER_SIZE

EVENT_MAP_OFFSET =
    HEADER_SIZE
    + VALIDITY_MAP_SIZE

FFT_DATA_OFFSET =
    HEADER_SIZE
    + VALIDITY_MAP_SIZE
    + EVENT_MAP_SIZE
```

For Version 2:

```text
VALIDITY_MAP_OFFSET = 8192
EVENT_MAP_OFFSET    = 8704
FFT_DATA_OFFSET     = 12304
```

## 4. JSON Header

### 4.1 Header Region and Termination

The Header occupies a fixed 8192-byte region at the beginning of the file.

- Write the JSON body in UTF-8 starting at offset 0, without a BOM.
- The top-level JSON value must be an object.
- The JSON body must not exceed 8191 bytes after UTF-8 encoding.
- Pad all remaining bytes immediately after the JSON body through offset 8191 with `0x00`.
- The first `0x00` terminates the JSON body. At least one terminating byte must be reserved.
- Formatting whitespace and line breaks may be included in the JSON body.
- Do not use 80-byte CARDs, ASCII SPACE padding, or `END_HEADER`.
- Represent NUL within JSON strings as `\u0000`; do not include an actual `0x00` byte in the body.
- If the size limit is exceeded, report an error. Do not truncate the JSON for storage.

```text
Offset 0                 : Start of the UTF-8 JSON body
Offset JSON_BYTE_LENGTH  : First 0x00 (body terminator)
Through offset 8191      : All remaining bytes are 0x00
Offset 8192              : Start of the Validity Map
```

### 4.2 JSON Fields

Retain the existing Header key names and use JSON types for numbers and
booleans. `NTP_SYNCED` is a JSON boolean. Integer fields use JSON numbers.
Latitude, longitude, and frequency resolution also use JSON numbers;
names and timestamps use strings.

The fields shown below are required. When time has not been synchronized,
`NTP_SYNC_TIME_UTC` must be an empty string.

This example uses a center frequency of 780 Hz.

```json
{
  "HRO_FILE_VERSION": 2,
  "HEADER_SIZE": 8192,
  "VALIDITY_MAP_SIZE": 512,
  "EVENT_MAP_SIZE": 3600,
  "EVENT_TYPE": "UINT8_BIT_FLAGS",
  "DATA_TYPE": "FLOAT32",
  "BYTE_ORDER": "LITTLE_ENDIAN",
  "FILE_START_UTC": "2026-10-04T00:00:00Z",
  "FILE_HOUR_LOCAL": "2026-10-04T09:00:00",
  "TIME_ZONE": "Asia/Tokyo",
  "START_TIME_SOURCE": "NTP",
  "NTP_SYNCED": true,
  "NTP_SYNC_TIME_UTC": "2026-10-03T23:55:11Z",
  "TIME_CONFLICTS": 0,
  "OBSERVER": "Bergamot JellyBeans[Matsue Astronomy Club]",
  "LOCATION": "Yonago, Tottori, JAPAN",
  "LONGITUDE": 133.345833,
  "LATITUDE": 35.443611,
  "RECEIVER": "RTL-SDR Blog V4",
  "ANTENNA": "Small Loop MK-3A",
  "RF_FREQUENCY_HZ": 53372000,
  "SDR_LO_FREQUENCY_HZ": 53612000,
  "SDR_SAMPLE_RATE_HZ": 960000,
  "FFT_SIZE": 8192,
  "FFT_OUTPUT_RATE_HZ": 8192,
  "FFT_RESOLUTION_HZ": 1.0,
  "FFT_CENTER_HZ": 780,
  "FFT_RANGE_HZ": 300,
  "FFT_MIN_HZ": 480,
  "FFT_MAX_HZ": 1080,
  "FFT_BIN_COUNT": 601,
  "RECORD_INTERVAL_SEC": 1,
  "RECORD_SLOTS": 3600,
  "DEVICE": "Raspberry Pi 5",
  "SOFTWARE": "Pi5-HRO",
  "SOFTWARE_VERSION": "0.1"
}
```

Record the actual values for the device, observation conditions, and time;
do not copy the example values unchanged. With a center frequency of
900 Hz, `FFT_MIN_HZ` is 600 and `FFT_MAX_HZ` is 1200.

Strings may contain UTF-8 characters, including Japanese. Readers may
ignore additional unknown keys. Duplicate keys are not permitted.

### 4.3 Validation When Reading

1. Read 8192 bytes from the beginning of the file. Report an error if fewer bytes are available.
2. Find the first `0x00` in this region. Report a missing-terminator error if none is found.
3. Verify that all Header bytes from the terminator onward are `0x00`.
4. Decode the bytes before the terminator as UTF-8 and parse the entire body with a JSON parser.
5. Reject invalid UTF-8, invalid JSON syntax, an empty body, multiple JSON values, and duplicate keys.
6. Validate the top-level object, required keys, types, format version, region sizes, and FFT parameters.
7. Verify that the file size matches the expected size calculated from the Header.

Do not identify the end of the body by searching for a closing brace `}`:
braces may occur inside strings. Delimit the body at the first `0x00`,
then validate it as JSON.

Use a separate, appropriate reader for formats other than Version 2.
Do not interpret them as Version 2.

## 5. Validity Map

The Validity Map indicates whether the FFT data for each one-second slot
is valid. It uses one bit per second.

```text
RECORD_SLOTS = 3600

Required bits  = 3600 bits
Required bytes = 450 bytes
```

Version 2 reserves 512 bytes for the Validity Map. The first 450 bytes
hold validity information for 3600 seconds. The remaining 62 bytes are
reserved.

### 5.1 Bit Order

Bit order is **LSB-first**.

```text
byte[0]
    bit 0 → second 0
    bit 1 → second 1
    bit 2 → second 2
    ...
    bit 7 → second 7

byte[1]
    bit 0 → second 8
    ...
    bit 7 → second 15
```

General formula:

```text
byte_index = second / 8
bit_index  = second % 8
```

The corresponding bit means:

```text
0 = Invalid FFT data
1 = Valid FFT data
```

Example in C/C++:

```cpp
bool valid =
    (validity_map[second / 8] &
     (1u << (second % 8))) != 0;
```

Reserved bytes must be `0x00` in Version 2.

## 6. Event Map

The Event Map stores event information for each one-second slot.

```text
EVENT_MAP_SIZE = 3600 bytes
RECORD_SLOTS   = 3600

1 second = 1 byte (uint8_t)
```

Event values are interpreted as **bit flags**.

### 6.1 Event Flags

Version 2 defines the following flags:

| Bit | Value | Event | Meaning |
| --- | --- | --- | --- |
| bit 0 | `0x01` | VISUAL | A human observer visually confirmed a meteor |
| bit 1 | `0x02` | Reserved | Reserved for future extensions |
| bit 2 | `0x04` | Reserved | Reserved for future extensions |
| bit 3 | `0x08` | Reserved | Reserved for future extensions |
| bit 4 | `0x10` | Reserved | Reserved for future extensions |
| bit 5 | `0x20` | Reserved | Reserved for future extensions |
| bit 6 | `0x40` | Reserved | Reserved for future extensions |
| bit 7 | `0x80` | Reserved | Reserved for future extensions |

No event is represented by:

```text
0x00 = NONE
```

For example:

```text
EVENT_MAP[927] = 0x01
```

This indicates a visual meteor event recorded 927 seconds after the file
start time. Because the Event Map uses bit flags, multiple event types
could be recorded in the same second if additional types are defined in
the future.

Bits not defined in Version 2 must be written as zero.

## 7. FFT Data

The FFT Data region stores one FFT spectrum per second.

The following are the basic conditions for Version 2. The center
frequency, `FFT_CENTER_HZ`, is configurable and is not fixed at 780 Hz.
Determine the stored frequency range from the configured center:

```text
FFT_RANGE_HZ = 300
FFT_MIN_HZ = FFT_CENTER_HZ - FFT_RANGE_HZ
FFT_MAX_HZ = FFT_CENTER_HZ + FFT_RANGE_HZ
FFT_BIN_COUNT = (FFT_MAX_HZ - FFT_MIN_HZ) / FFT_RESOLUTION_HZ + 1
              = 601  (FFT_RESOLUTION_HZ = 1.0)
```

The following example uses a center frequency of 780 Hz. The range
480–1080 Hz is not a fixed storage range.

```text
FFT_SIZE=8192
FFT_CENTER_HZ=780
FFT_RANGE_HZ=300
FFT_MIN_HZ=480
FFT_MAX_HZ=1080
FFT_BIN_COUNT=601

DATA_TYPE=FLOAT32
BYTE_ORDER=LITTLE_ENDIAN

RECORD_INTERVAL_SEC=1
RECORD_SLOTS=3600
```

`FFT_OUTPUT_RATE_HZ` denotes the sample rate of the signal supplied to
the FFT input. The file record interval is `RECORD_INTERVAL_SEC=1`;
it does not mean 8192 records per second.

Do not apply the screen's Display Level adjustment to stored FFT values.
The FLOAT32 type alone does not specify the value scale, such as linear
power, amplitude, or dB. The scale, normalization, and reference must be
verified when implementing Pi5 recording and added to this specification.

Each record consists of:

```text
601 × FLOAT32
```

FLOAT32 occupies four bytes:

```text
RECORD_SIZE = 601 × 4
            = 2404 bytes
```

The FFT Data region stores records in this order:

```text
record[0]       second 0
record[1]       second 1
...
record[3599]    second 3599
```

Within each record, FFT values are stored in ascending frequency order:

```text
FFT_MIN_HZ → FFT_MAX_HZ
```

## 8. Time and Slots

Each file covers one hour.

```text
RECORD_INTERVAL_SEC = 1
RECORD_SLOTS = 3600
```

The slot number equals the elapsed seconds from the file start time.

```text
09:00:00 → slot 0
09:00:01 → slot 1
...
09:15:27 → slot 927
...
09:59:59 → slot 3599
```

The Validity Map, Event Map, and FFT Data all use the same slot numbers.
For slot 927:

```text
Validity Map → Whether the FFT data for slot 927 is valid
Event Map    → Whether events are present in slot 927
FFT Data     → The spectrum for slot 927
```

## 9. Writing Files

Using the `.tmp` extension during observation is recommended to identify
an incomplete file.

```text
20260920_0900.hro.tmp
```

After the hour's recording is complete and the required data has been
successfully written, rename the file to:

```text
20260920_0900.hro
```

Viewers and analysis software can then normally process only `.hro` files.

## 10. Design Principles

The HRO File Format follows these principles:

- Store observation data and observation conditions in the same file.
- Make the data structure and observation conditions understandable from the Header alone.
- Provide a common format for Tab5, Raspberry Pi, Windows viewers, and other applications.
- Distinguish missing observation data from legitimate zero values.
- Store events, such as visual observations, on the same timeline as FFT data.
- Support future viewers, analysis processing, and AI analysis.
- Use a human-readable text Header.
- Keep binary data regions simple for fast and straightforward access.

## 11. Version

```text
HRO File Format Version : 2
Specification Language  : English
Document Edition        : 3
Revision Date           : 2026-10-04
Status                  : Draft
```

This specification is a Version 2 draft for the HRO system under
development. It may be revised as implementation validation proceeds.

## 12. Second-Edition Changes and Compatibility

| Item | First Edition | Second Edition |
| --- | --- | --- |
| FFT range (780 Hz center) | ±250 Hz | ±300 Hz |
| FFT_MIN_HZ | 530 | 480 |
| FFT_MAX_HZ | 1030 | 1080 |
| FFT_BIN_COUNT | 501 | 601 |
| One record | 2,004 bytes | 2,404 bytes |
| Entire FFT Data region | 7,214,400 bytes | 8,654,400 bytes |
| Entire file | 7,226,704 bytes | 8,666,704 bytes |

The third edition replaces the 80-byte fixed-length CARD header with
UTF-8 JSON. All bytes immediately after the JSON body through the end
of the Header region are padded with `0x00`.

The center frequency is configurable. The stored range is center ±300 Hz,
with 601 bins per second. The layout and starting offsets of the Validity
Map, Event Map, and FFT Data remain unchanged.

## 13. Third-Edition Changes and Compatibility

| Item | Second Edition (Format Version 1) | Third Edition (Format Version 2) |
| --- | --- | --- |
| Header body | 80-byte fixed-length CARDs | UTF-8 JSON object |
| Header terminator | END_HEADER CARD | First 0x00 |
| Unused region | ASCII SPACE | 0x00 |
| Numbers and booleans | Text | JSON numbers and booleans |
| Header region | 8192 bytes | 8192 bytes (unchanged) |
| FFT bins | 601 bins per second | 601 bins per second (unchanged) |
| Entire file | 8,666,704 bytes | 8,666,704 bytes (unchanged) |

This revision changes the specification. JSON support for the existing
`HroWriter` is to be implemented separately.

The Format Version 1 CARD header and Format Version 2 JSON header are
not compatible. Readers must inspect the beginning of the file to
identify the format and validate `HRO_FILE_VERSION` for JSON headers.
Do not guess how to read an unsupported format.
