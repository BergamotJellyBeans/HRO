idf_component_register(
    SRCS
        "main.cpp"
        "tab5_config.cpp"
        "tab5_config_validation.cpp"
        "tab5_runtime.cpp"
        "tab5_png.cpp"
        "tab5_storage.cpp"
        "tab5_display.cpp"
        "tab5_controls.cpp"
        "tab5_observation.cpp"
        "tab5_audio.cpp"
        "tab5_time.cpp"
        "tab5_wifi.cpp"
        "tab5_web_settings.cpp"
        "tab5_radio.cpp"
        "tab5_dsp.cpp"
        "tab5_fft.cpp"
        "tab5_helpers.cpp"
        "tab5_application.cpp"

    INCLUDE_DIRS "."
    REQUIRES hro_common tab5
    PRIV_REQUIRES
        fatfs sdmmc esp_driver_sdmmc
        esp_timer esp_wifi esp_event esp_netif
        nvs_flash esp_http_server lwip
        esp_rtl_sdr m5stack__m5unified espressif__esp-dsp
)
