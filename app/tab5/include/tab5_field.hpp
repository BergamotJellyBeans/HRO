#pragma once
#include "esp_http_server.h"
namespace hro::tab5::app {
void field_init();
void poll_phone_field();
esp_err_t register_field_handlers(httpd_handle_t server);
double observation_latitude(double saved);
double observation_longitude(double saved);
bool phone_clock_active();
void clear_phone_clock_source();
}
