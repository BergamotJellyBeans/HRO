#pragma once
namespace stick {
bool audio_begin();
bool audio_ready();
bool audio_busy();
unsigned audio_stack_free();
unsigned audio_queue_size();
void audio_ack(unsigned count = 1);
void audio_connected();
}
