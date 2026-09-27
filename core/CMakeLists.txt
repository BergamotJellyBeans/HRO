add_library(hro_core
    src/hro_format.cpp
    src/hro_header.cpp
    src/hro_writer.cpp
    src/dsp/fs4_rotator.cpp

)

target_include_directories(hro_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
