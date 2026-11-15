#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace hro::pi {
// PNG placement only. Original timestamps and sequence numbers remain unchanged.
// One generated FFT represents one second of samples at the configured rate.
class PngTimeAlignment {
public:
    struct Placement { std::int64_t second; bool reanchored; };
    std::optional<Placement> place(std::int64_t timestampMs, std::uint64_t sequence) {
        if (timestampMs < 1704067200000LL || timestampMs >= 4102444800000LL) return std::nullopt;
        const auto nearest = (timestampMs + 500) / 1000;
        if (!initialized_) return accept(timestampMs, sequence, nearest, false);
        if (sequence == lastSequence_) return std::nullopt; // Duplicate UDP packet.
        if (sequence < lastSequence_) {
            // A restarted producer has a lower sequence and a newer timestamp.
            // Delayed/out-of-order old packets must not overwrite current spectra.
            if (timestampMs <= lastTimestamp_ || nearest <= lastSecond_) return std::nullopt;
            return accept(timestampMs, sequence, nearest, true);
        }
        const auto delta = sequence - lastSequence_;
        if (delta > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() - lastSecond_))
            return std::nullopt;
        const auto predicted = lastSecond_ + static_cast<std::int64_t>(delta);
        // Keep bounded completion-time jitter from moving a record to another slot.
        // Missing sequence numbers still consume seconds; they are never compacted.
        if (predicted <= 4102444800LL && timestampMs >= predicted * 1000 - 1500 &&
            timestampMs <= predicted * 1000 + 1500)
            return accept(timestampMs, sequence, predicted, false);
        // A genuine pause, large clock change or accumulated clock-rate drift needs
        // a new wall-clock anchor. Never go back to an already finalized PNG block.
        if (nearest <= lastSecond_) return std::nullopt;
        return accept(timestampMs, sequence, nearest, true);
    }
private:
    std::optional<Placement> accept(std::int64_t stamp, std::uint64_t seq,
                                    std::int64_t second, bool reanchored) {
        initialized_ = true; lastTimestamp_ = stamp; lastSequence_ = seq; lastSecond_ = second;
        return Placement{second, reanchored};
    }
    bool initialized_ = false;
    std::int64_t lastTimestamp_ = 0, lastSecond_ = 0;
    std::uint64_t lastSequence_ = 0;
};
}
