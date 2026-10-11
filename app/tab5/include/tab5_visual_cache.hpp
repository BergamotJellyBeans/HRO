#pragma once
#include "hro_visual_packet.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hro::tab5::app {
// Keep IDs for 30 seconds, longer than the Stick's 10-second retry limit.
// Never evict unexpired IDs: overload must not turn a retry into a new event.
class VisualCache {
public:
    static constexpr std::size_t CAPACITY = 128;
    static constexpr std::int64_t RETENTION_US = 30000000;
    enum class Result { New, Duplicate, Full };
    Result accept(const hro::visual::Request& request, std::int64_t now_us,
                  std::uint64_t received_ms, std::uint64_t& accepted_ms) {
        Entry* free_slot = nullptr;
        for (auto& entry : entries_) {
            if (entry.used && now_us - entry.accepted_us >= RETENTION_US) entry.used = false;
            if (!entry.used) { if (!free_slot) free_slot = &entry; continue; }
            if (std::strcmp(entry.request.stick_id, request.stick_id) == 0 &&
                std::strcmp(entry.request.event_id, request.event_id) == 0 &&
                entry.request.version == request.version && entry.request.sequence == request.sequence && entry.request.meteor_count == request.meteor_count) {
                accepted_ms = entry.received_ms;
                return Result::Duplicate;
            }
        }
        if (!free_slot) return Result::Full;
        *free_slot = {true, request, now_us, received_ms};
        accepted_ms = received_ms;
        return Result::New;
    }
    // Queue admission failed: allow the same ID to be admitted on its next retry.
    void forget(const hro::visual::Request& request) {
        for (auto& entry : entries_) {
            if (entry.used && !std::strcmp(entry.request.stick_id, request.stick_id) &&
                !std::strcmp(entry.request.event_id, request.event_id)) entry.used = false;
        }
    }
private:
    struct Entry {
        bool used = false;
        hro::visual::Request request;
        std::int64_t accepted_us = 0;
        std::uint64_t received_ms = 0;
    };
    Entry entries_[CAPACITY] = {};
};
}
