# HRO - Radio Meteor Observation System

HRO is an open-source radio meteor observation system currently under development.

This project is developed by **Bergamot JellyBeans** as a project of
**Matsue Astronomy Club (松江星の会), Japan**.

The project aims to build a reliable platform for continuous meteor radio observation using software-defined radio (SDR), with support for both stationary and portable observation systems.

## Pi5 Setup and Configuration Examples

[Pi5-HROの構築手順：NTP・RTL-SDR・ビルド・起動（日本語）](docs/Pi5_Installation.md)

[Pi5の設定・NVMeマウント・SMB共有（日本語）](docs/Pi5_Setup.md)

Configuration examples based on the running Pi5 station are available in
[`examples/pi5/`](examples/pi5/). Replace observer details and the SSD UUID
before use. The Samba and fstab examples are fragments, not complete replacements.

## HRO recording and Archive Viewer

Pi5のhro-engineは、生成したFFT（中心周波数±300 Hzの601点）を生成順に保存します。
各レコードはUnix時刻（ミリ秒）とシーケンスを持ちます。同じ秒のFFTも保持します。
PNG・JSON保存はhro-pngが担当します。

- [HRO記録・導入と確認](app/pi/hro-png/HRO_RECORDING.md)
- [Archive Viewerの操作・反映手順](docs/Archive_Viewer.md)
- [日本語：HRO v2連続レコードと旧秒スロット形式](docs/HRO_File_Format_Specification.md)
- [English: sequential records and legacy format](docs/HRO_File_Format_Specification_EN.md)

The archive viewer displays partial PNG observations, legacy HRO files and sequential FFT records,
including complete records in temporary files. Recording and viewer tests have passed on Pi5;
long-duration operation is currently under observation. The existing directory layout is retained.

## Project Goals

- Continuous 24/7 radio meteor observation
- Raspberry Pi 5 based observation station
- RTL-SDR signal acquisition
- FFT-based meteor echo analysis
- Reproducible relative dB measurements
- Hourly observation data storage in the HRO file format
- Browser-based live monitoring and station configuration
- Portable observation using M5Stack Tab5
- Cross-platform observation data viewer
- Future AI-assisted meteor echo detection and classification

## Current Development

The Raspberry Pi 5 observation station is currently under active development.

Implemented components include:

- HRO observation file format and writer
- Safe observation file finalization
- RTL-SDR sample acquisition
- DSP frequency conversion and resampling pipeline
- Hann window and complex FFT processing
- Configuration file reader, validation and safe saving
- Browser-based station settings interface
- Base design for the live observation monitor

The live monitor, spectrum processing, waterfall display, audio streaming, and integration of the complete observation pipeline are still under development.

## System Concept

```text
RTL-SDR
   |
   v
Raspberry Pi 5
   |
   +-- DSP / FFT
   |
   +-- HRO observation files
   |
   +-- Web Live Monitor
   |
   +-- Tab5 Remote Monitor
   |
   +-- Future AI Analysis
```
