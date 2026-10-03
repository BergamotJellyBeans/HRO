get_filename_component(HRO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
idf_component_register(
    SRCS "${HRO_ROOT}/core/src/dsp/fs4_rotator.cpp"
         "${HRO_ROOT}/core/src/dsp/hann_window.cpp"
         "${HRO_ROOT}/core/src/dsp/nco_shifter.cpp"
         "${HRO_ROOT}/core/src/dsp/resampler_16_125.cpp"
    INCLUDE_DIRS "${HRO_ROOT}/core/include"
)
