#!/usr/bin/env python3
"""Read HRO v2 metadata and optionally a one-second FFT record (standard library only)."""
import argparse
import json
import math
import os
import struct

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result

def read_header(stream):
    block = stream.read(8192)
    if len(block) != 8192:
        raise ValueError("Truncated header")
    end = block.find(b'\0')
    if end < 0 or any(block[end:]):
        raise ValueError("Missing NUL terminator or nonzero padding")
    def invalid_constant(value):
        raise ValueError(f"Invalid JSON constant: {value}")
    header = json.loads(block[:end].decode('utf-8'), object_pairs_hook=unique_object,
                        parse_constant=invalid_constant)
    if not isinstance(header, dict):
        raise ValueError("Header must be a JSON object")
    sequential = header.get('RECORD_LAYOUT') == 'SEQUENTIAL_FFT_V1'
    if 'RECORD_LAYOUT' in header and not sequential:
        raise ValueError('Unsupported record layout')
    for key, expected in {
        'HRO_FILE_VERSION': 2, 'HEADER_SIZE': 8192, 'VALIDITY_MAP_SIZE': 0 if sequential else 512,
        'EVENT_MAP_SIZE': 0 if sequential else 3600, 'RECORD_SLOTS': 0 if sequential else 3600, 'RECORD_INTERVAL_SEC': 0 if sequential else 1,
        'FFT_BIN_COUNT': 601, 'FFT_SIZE': 8192, 'BYTE_ORDER': 'LITTLE_ENDIAN',
        'DATA_TYPE': 'FLOAT32', 'EVENT_TYPE': 'UINT8_BIT_FLAGS',
        'FFT_VALUE_SCALE': 'DB_POWER', 'FFT_WINDOW': 'PERIODIC_HANN',
        'FFT_NORMALIZATION': 'NONE', 'FFT_POWER_EPSILON': 1e-20,
    }.items():
        value = header.get(key)
        if value != expected or isinstance(value, bool):
            raise ValueError(f"Unsupported or invalid {key}")
    center = header.get('FFT_CENTER_HZ')
    if not isinstance(center, (int, float)) or isinstance(center, bool) or not math.isfinite(center):
        raise ValueError('Invalid center frequency')
    if (header.get('FFT_MIN_HZ') != center - 300 or header.get('FFT_MAX_HZ') != center + 300
            or header.get('FFT_RESOLUTION_HZ') != 1 or header.get('FFT_RANGE_HZ') != 300):
        raise ValueError('Inconsistent frequency range')
    if sequential:
        if (header.get('RECORD_SIZE') != 2420 or header.get('DATA_OFFSET') != 8192 or
            header.get('RECORD_FIELDS') != ['TIMESTAMP_MS_INT64', 'SEQUENCE_UINT64', 'FFT_FLOAT32']):
            raise ValueError('Invalid sequential layout')
        size = os.fstat(stream.fileno()).st_size
        if (size - 8192) % 2420:
            raise ValueError('Incomplete final record; preserve the file for recovery')
        return header
    if os.fstat(stream.fileno()).st_size != 12304 + 3600 * 601 * 4:
        raise ValueError('Unexpected HRO file size')
    return header

def read_second(stream, slot):
    if not 0 <= slot < 3600:
        raise ValueError('Second must be in 0..3599')
    stream.seek(8192 + slot // 8)
    valid = bool(stream.read(1)[0] & (1 << (slot % 8)))
    stream.seek(8704 + slot)
    event = stream.read(1)[0]
    spectrum = None
    if valid:
        stream.seek(12304 + slot * 601 * 4)
        spectrum = list(struct.unpack('<601f', stream.read(601 * 4)))
        if not all(math.isfinite(value) for value in spectrum):
            raise ValueError('Non-finite FFT value in valid record')
    return {'second': slot, 'valid': valid, 'visual': bool(event & 1), 'fft_db': spectrum}

def read_record(stream, index):
    count = (os.fstat(stream.fileno()).st_size - 8192) // 2420
    if not 0 <= index < count:
        raise ValueError(f'Record must be in 0..{count-1}')
    stream.seek(8192 + index * 2420)
    timestamp, sequence, *bins = struct.unpack('<qQ601f', stream.read(2420))
    if not all(math.isfinite(value) for value in bins):
        raise ValueError('Non-finite FFT value')
    return {'index': index, 'timestamp_ms': timestamp, 'sequence': sequence, 'fft_db': bins}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--second', type=int)
    selection.add_argument('--record', type=int)
    args = parser.parse_args()
    try:
        with open(args.file, 'rb') as stream:
            header = read_header(stream)
            sequential = header.get('RECORD_LAYOUT') == 'SEQUENTIAL_FFT_V1'
            if args.second is not None and sequential:
                raise ValueError('Use --record for sequential FFT files')
            if args.record is not None and not sequential:
                raise ValueError('Use --second for legacy slot files')
            result = header
            if args.second is not None:
                result = {'header': header, 'record': read_second(stream, args.second)}
            elif args.record is not None:
                result = {'header': header, 'record': read_record(stream, args.record)}
        print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, f'HRO read error: {error}\n')
if __name__ == '__main__':
    main()
