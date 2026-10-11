idf_component_register(
    SRCS "main.cpp" "../src/stick_application.cpp" "../src/stick_display.cpp" "../src/stick_buttons.cpp" "../src/stick_network.cpp" "../src/stick_time.cpp" "../src/stick_visual.cpp" "../src/stick_audio.cpp" "../src/stick_led.cpp"
    INCLUDE_DIRS "../include"
    PRIV_INCLUDE_DIRS "../../../core/include"
    EMBED_FILES "../assets/hro_logo.png"
    PRIV_REQUIRES esp_hw_support log freertos esp_driver_gpio esp_driver_i2s esp_timer esp_wifi esp_event esp_netif nvs_flash lwip m5stack__m5gfx
)
