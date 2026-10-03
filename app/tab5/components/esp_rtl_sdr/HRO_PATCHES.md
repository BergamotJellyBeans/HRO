# HRO local driver patch

Based on hardcoreerik/esp-rtl-sdr commit 1cd19d1363daea49b013c2d28a25750fcbfcff78.
The upstream license files are retained.

Sample-rate quantization reconstructs divider bit 28 from bit 27 before
calculating the effective rate, following RTL-SDR Blog librtlsdr:
https://github.com/rtlsdrblog/rtl-sdr-blog/blob/master/src/librtlsdr.c

This keeps 256000 sps unchanged when start() quantizes it repeatedly.
Previously the 28-bit register value alone yielded 593814 sps, which was
rejected by the second quantization inside run_sample_rate().

Host regressions cover 250000, 256000 and 300000 sps; existing high-rate,
invalid-rate and other policy checks remain enabled.
