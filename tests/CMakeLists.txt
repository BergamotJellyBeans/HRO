add_executable(test_observation_file_manager
    test_observation_file_manager.cpp
)

target_link_libraries(test_observation_file_manager
    PRIVATE
        hro_core
        hro_raspberrypi
)

add_test(
    NAME observation_file_manager
    COMMAND test_observation_file_manager
)

add_executable(test_rtl_sdr_source
    test_rtl_sdr_source.cpp
)

target_link_libraries(test_rtl_sdr_source
    PRIVATE
        hro_raspberrypi
)

add_executable(test_fs4_rotator
    test_fs4_rotator.cpp
)

target_link_libraries(test_fs4_rotator
    PRIVATE
        hro_core
)

add_test(
    NAME fs4_rotator
    COMMAND test_fs4_rotator
)

add_executable(test_decimator_15
    test_decimator_15.cpp
)

target_link_libraries(test_decimator_15
    PRIVATE
        hro_core
)

add_test(
    NAME decimator_15
    COMMAND test_decimator_15
)
