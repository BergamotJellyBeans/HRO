idf_component_register(
    SRCS
        "main.cpp"
        "../src/tab5_config.cpp"
        "../src/tab5_config_validation.cpp"
        "../src/tab5_runtime.cpp"
        "../src/tab5_png.cpp"
        "../src/tab5_storage.cpp"
        "../src/tab5_display.cpp"
        "../src/tab5_controls.cpp"
        "../src/tab5_observation.cpp"
        "../src/tab5_audio.cpp"
        "../src/tab5_time.cpp"
        "../src/tab5_wifi.cpp"
        "../src/tab5_web_settings.cpp"
        "../src/tab5_radio.cpp"
        "../src/tab5_dsp.cpp"
        "../src/tab5_fft.cpp"
        "../src/tab5_helpers.cpp"
        "../src/tab5_application.cpp"

    INCLUDE_DIRS "../include"
    PRIV_INCLUDE_DIRS "../src/internal"
    REQUIRES hro_common tab5
    PRIV_REQUIRES
        fatfs sdmmc esp_driver_sdmmc
        esp_timer esp_wifi esp_event esp_netif
        nvs_flash esp_http_server lwip
        esp_rtl_sdr m5stack__m5unified espressif__esp-dsp
)
