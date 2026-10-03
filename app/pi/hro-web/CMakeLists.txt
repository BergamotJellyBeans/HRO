add_executable(hro-web
    main.cpp
    tab5_udp_server.cpp
)

target_link_libraries(hro-web
    PRIVATE
        hro_core
        cpp-httplib
)
