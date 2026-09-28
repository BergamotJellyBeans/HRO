# HRO - Radio Meteor Observation System

HRO is an open-source radio meteor observation system currently under development.

The project aims to build a reliable platform for continuous meteor radio observation using software-defined radio (SDR), with support for both stationary and portable observation systems.

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
