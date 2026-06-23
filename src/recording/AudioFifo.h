#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace composer::recording {

// Fixed-capacity single-producer/single-consumer FIFO. The audio callback only copies samples and
// updates atomics; allocation, locks, filesystem calls and diagnostic formatting stay off-thread.
class AudioFifo final {
public:
    AudioFifo(std::size_t capacityFrames, std::size_t channels);

    [[nodiscard]] bool push(const float* const* inputs, std::size_t availableInputChannels,
        std::size_t firstInputChannel, std::size_t inputOffsetFrames,
        std::size_t frameCount) noexcept;
    [[nodiscard]] std::size_t pop(float* interleaved, std::size_t maximumFrames) noexcept;

    [[nodiscard]] std::size_t availableFrames() const noexcept;
    [[nodiscard]] std::size_t capacityFrames() const noexcept { return capacityFrames_; }
    [[nodiscard]] std::size_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::size_t highWaterFrames() const noexcept {
        return highWaterFrames_.load(std::memory_order_relaxed);
    }

private:
    std::vector<float> samples_;
    std::size_t capacityFrames_{};
    std::size_t channels_{};
    std::atomic<std::uint64_t> readFrame_{0};
    std::atomic<std::uint64_t> writeFrame_{0};
    std::atomic<std::size_t> highWaterFrames_{0};
};

}  // namespace composer::recording
