#pragma once
#include "tab5_config.h"
#include "esp_rtl_sdr.h"

namespace hro::tab5::app {
bool rtl_recognized();
HroTuning make_hro_tuning( void );
void rtl_event_callback( esp_rtl_sdr_event_t event, const void *payload, void *user_ctx );
void hro_radio_task( void * );
}
