#pragma once
#include "esp_http_server.h"

namespace hro::tab5::app {
esp_err_t station_get_handler( httpd_req_t *req );
esp_err_t station_save_handler( httpd_req_t *req );
}
