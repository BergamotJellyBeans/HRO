#pragma once

namespace hro::tab5::app {
void play_touch_beep( void );
void audio_config_changed( void );
void save_audio_config_if_needed( void );
void hro_audio_task( void *arg );
}
