cmake_minimum_required(VERSION 3.16)
# Independent ESP-IDF project; never include this from the host root build.
if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "M5StickS3 requires IDF_TARGET=esp32s3")
endif()
set(IDF_TARGET "esp32s3")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(M5StickS3_HRO)
