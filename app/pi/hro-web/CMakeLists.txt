add_executable(hro-web
    main.cpp
)

target_link_libraries(hro-web
    PRIVATE
        hro_core
        cpp-httplib
)
