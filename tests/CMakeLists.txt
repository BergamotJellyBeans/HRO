if(TARGET hro_raspberrypi)
add_executable(test_observation_file_manager
    test_observation_file_manager.cpp
)

target_link_libraries(test_observation_file_manager
    PRIVATE
        hro_core
        hro_raspberrypi
)

add_test(
    NAME observation_file_manager
    COMMAND test_observation_file_manager
)

add_executable(test_rtl_sdr_source
    test_rtl_sdr_source.cpp
)

target_link_libraries(test_rtl_sdr_source
    PRIVATE
        hro_raspberrypi
)

endif()

add_executable(test_fs4_rotator
    test_fs4_rotator.cpp
)

target_link_libraries(test_fs4_rotator
    PRIVATE
        hro_core
)

add_test(
    NAME fs4_rotator
    COMMAND test_fs4_rotator
)

add_executable(test_decimator_15
    test_decimator_15.cpp
)

target_link_libraries(test_decimator_15
    PRIVATE
        hro_core
)

add_test(
    NAME decimator_15
    COMMAND test_decimator_15
)

add_executable(test_resampler_16_125
    test_resampler_16_125.cpp
)

target_link_libraries(test_resampler_16_125
    PRIVATE
        hro_core
)

add_test(
    NAME resampler_16_125
    COMMAND test_resampler_16_125
)

add_executable(test_nco_shifter
    test_nco_shifter.cpp
)

target_link_libraries(test_nco_shifter
    PRIVATE hro_core
)

add_test(
    NAME nco_shifter
    COMMAND test_nco_shifter
)

add_executable(test_dsp_chain
    test_dsp_chain.cpp
)

target_link_libraries(test_dsp_chain
    PRIVATE hro_core
)

add_test(
    NAME dsp_chain
    COMMAND test_dsp_chain
)

add_executable(test_hann_window
    test_hann_window.cpp
)

target_link_libraries(test_hann_window
    PRIVATE hro_core
)

add_test(
    NAME hann_window
    COMMAND test_hann_window
)

add_executable(test_complex_fft
    test_complex_fft.cpp
)

target_link_libraries(test_complex_fft
    PRIVATE hro_core
)

add_test(
    NAME complex_fft
    COMMAND test_complex_fft
)

add_executable(test_hro_config
    test_hro_config.cpp
)

target_link_libraries(test_hro_config
    PRIVATE hro_core
)

add_test(
    NAME hro_config
    COMMAND test_hro_config
)

# Platform-free checks for the shared plot specification and Tab5 RF frontend.
add_executable(test_hro_plot test_hro_plot.cpp)
target_link_libraries(test_hro_plot PRIVATE hro_core)
add_test(NAME hro_plot COMMAND test_hro_plot)

add_executable(test_tab5_frontend test_tab5_frontend.cpp
    ../platform/tab5/tab5_frontend.cpp)
target_include_directories(test_tab5_frontend PRIVATE ../platform/tab5/include)
target_link_libraries(test_tab5_frontend PRIVATE hro_core)
add_test(NAME tab5_frontend COMMAND test_tab5_frontend)

# Tab5 application rules can be checked on a Mac without hardware or ESP-IDF.
add_executable(test_tab5_app_helpers test_tab5_app_helpers.cpp
    ../app/tab5/src/tab5_helpers.cpp
    ../app/tab5/src/tab5_config_validation.cpp)
target_include_directories(test_tab5_app_helpers PRIVATE
    ../app/tab5/include ../platform/tab5/include)
target_link_libraries(test_tab5_app_helpers PRIVATE hro_core)
add_test(NAME tab5_app_helpers COMMAND test_tab5_app_helpers)
