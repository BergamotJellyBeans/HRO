# HRO - Radio Meteor Observation System

HRO is an open-source radio meteor observation system currently under development.

This project is developed by **Bergamot JellyBeans** as a project of
**Matsue Astronomy Club (松江星の会), Japan**.

The project aims to build a reliable platform for continuous meteor radio observation using software-defined radio (SDR), with support for both stationary and portable observation systems.

## Pi5 Setup and Configuration Examples

[Pi5の設定・NVMeマウント・SMB共有（日本語）](docs/Pi5_Setup.md)

Configuration examples based on the running Pi5 station are available in
[`examples/pi5/`](examples/pi5/). Replace observer details and the SSD UUID
before use. The Samba and fstab examples are fragments, not complete replacements.

## HRO File Format Specification

[日本語：HROファイル形式仕様書 第3版](docs/HRO_File_Format_Specification.md) |
[English: HRO File Format Specification, Third Edition](docs/HRO_File_Format_Specification_EN.md)

HRO File Version 2のドラフト仕様です。1時間分のFFTデータと観測条件を、
UTF-8 JSONヘッダ、Validity Map、Event Map、FFT Dataに格納します。
仕様書の版とファイル形式のVersionは別の番号です。

Draft specification for HRO File Version 2. Each file stores one hour of FFT
data and observation metadata. Document edition and file format version
are separate identifiers.

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
