add_library(hro_raspberrypi
    observation_file_manager.cpp
    rtl_sdr_source.cpp
)

target_include_directories(hro_raspberrypi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hro_raspberrypi
    PUBLIC
        hro_core
        rtlsdr
)