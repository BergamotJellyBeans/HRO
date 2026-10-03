idf_component_register(
    SRCS "tab5_frontend.cpp" "tab5_sdcard.cpp" "tab5_rtc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hro_common
    PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc m5stack__m5unified
)
