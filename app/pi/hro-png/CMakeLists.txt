find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED cairo)

add_executable(hro-png
    main.cpp
)

target_include_directories(hro-png
    PRIVATE
        ${CAIRO_INCLUDE_DIRS}
)

target_link_directories(hro-png
    PRIVATE
        ${CAIRO_LIBRARY_DIRS}
)

target_link_libraries(hro-png
    PRIVATE
        ${CAIRO_LIBRARIES}
)

target_link_libraries(hro-png
    PRIVATE
        hro_core
        ${CAIRO_LIBRARIES}
)