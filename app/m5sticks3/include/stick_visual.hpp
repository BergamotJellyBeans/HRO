#pragma once
#include "stick_network.hpp"
namespace stick {
const char* visual_status();
void visual_poll(const NetworkView& network);
void visual_send(const NetworkView& network, const char* stick_id, unsigned meteor_count = 1);
}
