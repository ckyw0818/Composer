#include "recording/AudioFifo.h"

#include <algorithm>

namespace composer::recording {

AudioFifo::AudioFifo(const std::size_t capacityFrames, const std::size_t channels)
    : samples_(std::max<std::size_t>(1, capacityFrames) * std::max<std::size_t>(1, channels), 0.0F),
      capacityFrames_(std::max<std::size_t>(1, capacityFrames)),
      channels_(std::max<std::size_t>(1, channels)) {}

bool AudioFifo::push(const float* const* inputs, const std::size_t availableInputChannels,
    const std::size_t firstInputChannel, const std::size_t inputOffsetFrames,
    const std::size_t frameCount) noexcept {
    const auto write = writeFrame_.load(std::memory_order_relaxed);
    const auto read = readFrame_.load(std::memory_order_acquire);
    const auto used = static_cast<std::size_t>(write - read);
    if (frameCount > capacityFrames_ - used) return false;

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const auto destinationFrame = static_cast<std::size_t>((write + frame) % capacityFrames_);
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            const auto inputChannel = firstInputChannel + channel;
            const float sample = inputs != nullptr && inputChannel < availableInputChannels
                    && inputs[inputChannel] != nullptr
                ? inputs[inputChannel][inputOffsetFrames + frame]
                : 0.0F;
            samples_[destinationFrame * channels_ + channel] = sample;
        }
    }
    writeFrame_.store(write + frameCount, std::memory_order_release);
    const auto level = used + frameCount;
    auto highWater = highWaterFrames_.load(std::memory_order_relaxed);
    while (level > highWater
        && !highWaterFrames_.compare_exchange_weak(
            highWater, level, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    return true;
}

std::size_t AudioFifo::pop(float* const interleaved, const std::size_t maximumFrames) noexcept {
    const auto read = readFrame_.load(std::memory_order_relaxed);
    const auto write = writeFrame_.load(std::memory_order_acquire);
    const auto frames = std::min<std::size_t>(maximumFrames, static_cast<std::size_t>(write - read));
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto sourceFrame = static_cast<std::size_t>((read + frame) % capacityFrames_);
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            interleaved[frame * channels_ + channel] = samples_[sourceFrame * channels_ + channel];
        }
    }
    readFrame_.store(read + frames, std::memory_order_release);
    return frames;
}

std::size_t AudioFifo::availableFrames() const noexcept {
    const auto read = readFrame_.load(std::memory_order_acquire);
    const auto write = writeFrame_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(write - read);
}

}  // namespace composer::recording
