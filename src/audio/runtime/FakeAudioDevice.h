#pragma once

#include "audio/contracts/AudioRenderer.h"

#include <cstdint>

namespace composer::audio {

struct FakeDeviceReport final {
    std::uint64_t sampleHash{};
    std::size_t callbackCount{};
    double p99Milliseconds{};
    double peakMagnitude{};
};

class FakeAudioDevice final {
public:
    explicit FakeAudioDevice(AudioSpec spec);

    [[nodiscard]] FakeDeviceReport render(
        AudioRenderer& renderer,
        domain::ProjectSample totalFrames) const;

private:
    AudioSpec spec_{};
};

}  // namespace composer::audio
