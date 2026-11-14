"""Reader regression tests; standard library only."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('read_hro', Path(__file__).parents[1] / 'tools/read_hro.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)

class ReaderTest(unittest.TestCase):
    def header(self, sequential):
        h = dict(HRO_FILE_VERSION=2, HEADER_SIZE=8192, VALIDITY_MAP_SIZE=512,
                 EVENT_MAP_SIZE=3600, RECORD_SLOTS=3600, RECORD_INTERVAL_SEC=1,
                 FFT_BIN_COUNT=601, FFT_SIZE=8192, BYTE_ORDER='LITTLE_ENDIAN',
                 DATA_TYPE='FLOAT32', EVENT_TYPE='UINT8_BIT_FLAGS', FFT_VALUE_SCALE='DB_POWER',
                 FFT_WINDOW='PERIODIC_HANN', FFT_NORMALIZATION='NONE', FFT_POWER_EPSILON=1e-20,
                 FFT_CENTER_HZ=900, FFT_MIN_HZ=600, FFT_MAX_HZ=1200, FFT_RESOLUTION_HZ=1, FFT_RANGE_HZ=300)
        if sequential:
            h.update(RECORD_LAYOUT='SEQUENTIAL_FFT_V1', RECORD_SIZE=2420, DATA_OFFSET=8192,
                     RECORD_FIELDS=['TIMESTAMP_MS_INT64', 'SEQUENCE_UINT64', 'FFT_FLOAT32'],
                     VALIDITY_MAP_SIZE=0, EVENT_MAP_SIZE=0, RECORD_SLOTS=0, RECORD_INTERVAL_SEC=0)
        return json.dumps(h).encode().ljust(8192, b'\0')

    def test_sequential(self):
        with tempfile.TemporaryFile() as f:
            f.write(self.header(True))
            for timestamp, sequence in [(1791072000123, 0), (1791072000123, 1), (1791071999000, 2)]:
                f.write(struct.pack('<qQ601f', timestamp, sequence, *([-12.5] * 601)))
            f.seek(0)
            self.assertEqual(reader.read_header(f)['RECORD_LAYOUT'], 'SEQUENTIAL_FFT_V1')
            self.assertEqual(reader.read_record(f, 1)['timestamp_ms'], 1791072000123)
            self.assertEqual(reader.read_record(f, 2)['sequence'], 2)
            self.assertEqual(reader.read_record(f, 2)['fft_db'][0], -12.5)
            with self.assertRaises(ValueError): reader.read_record(f, 3)
            f.seek(0, 2); f.write(b'broken'); f.seek(0)
            with self.assertRaises(ValueError): reader.read_header(f)

    def test_legacy(self):
        with tempfile.TemporaryFile() as f:
            f.write(self.header(False))
            f.truncate(8666704)
            f.seek(0)
            reader.read_header(f)
            self.assertFalse(reader.read_second(f, 0)['valid'])

if __name__ == '__main__': unittest.main()
