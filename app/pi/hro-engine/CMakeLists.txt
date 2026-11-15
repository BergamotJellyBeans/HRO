add_executable(hro-engine
    main.cpp
    ../hro-png/hourly_hro.cpp
)

target_link_libraries(hro-engine
    PRIVATE
        hro_core
        hro_raspberrypi
)

target_include_directories(hro-engine PRIVATE ../hro-png ${HRO_JSON_INCLUDE_DIR})
