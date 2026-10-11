#pragma once
#include <cstddef>
#include "esp_err.h"

namespace hro::tab5::app {
// Same eFuse base MAC suffix used by the existing SoftAP SSID.
esp_err_t wifi_device_name(char* name, size_t size);
bool wifi_sta_ready();
bool load_wifi_settings( char *ssid, size_t ssid_size, char *password, size_t password_size );
void wifi_start_ap( const char *saved_ssid, const char *saved_password );
}
