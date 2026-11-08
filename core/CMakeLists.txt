add_library(hro_core
    src/hro_format.cpp
    src/hro_header.cpp
    src/hro_writer.cpp
    src/hro_config.cpp
    src/dsp/fs4_rotator.cpp
    src/dsp/decimator_15.cpp
    src/dsp/resampler_16_125.cpp
    src/dsp/nco_shifter.cpp
    src/dsp/hann_window.cpp
    src/dsp/complex_fft.cpp
)

target_include_directories(hro_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Already used by hro-web; JSON metadata parsing is also needed by the HRO writer.
find_path(HRO_JSON_INCLUDE_DIR nlohmann/json.hpp)
if(NOT HRO_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp is required (Pi: nlohmann-json3-dev)")
endif()
target_include_directories(hro_core PRIVATE ${HRO_JSON_INCLUDE_DIR})
