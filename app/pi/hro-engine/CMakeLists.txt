add_executable(hro-engine
    main.cpp
)

target_link_libraries(hro-engine
    PRIVATE
        hro_core
        hro_raspberrypi
)
