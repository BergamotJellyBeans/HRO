add_executable(hro-web
    main.cpp
    archive_hro.cpp
    tab5_udp_server.cpp
)

target_link_libraries(hro-web
    PRIVATE
        hro_core
        cpp-httplib
)

target_include_directories(hro-web PRIVATE ${HRO_JSON_INCLUDE_DIR})
