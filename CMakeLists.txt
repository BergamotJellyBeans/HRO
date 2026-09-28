cmake_minimum_required(VERSION 3.16)

project(HRO LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(core)
add_subdirectory(platform/raspberrypi)
enable_testing()
add_subdirectory(tests)

add_subdirectory(app/pi/hro-web)
add_subdirectory(app/pi/hro-engine)
