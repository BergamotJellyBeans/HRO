cmake_minimum_required(VERSION 3.16)

project(HRO LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)
option(HRO_BUILD_RASPBERRYPI "Build Raspberry Pi adapters and applications" ON)
if(HRO_BUILD_RASPBERRYPI)
    add_subdirectory(platform/raspberrypi)
endif()
enable_testing()
add_subdirectory(tests)

if(HRO_BUILD_RASPBERRYPI)
    add_subdirectory(app/pi/hro-web)
    add_subdirectory(app/pi/hro-engine)
    add_subdirectory(app/pi/hro-png)
endif()
