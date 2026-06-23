#pragma once

#include "recording/RecordingSession.h"

#include <cstddef>
#include <cstdint>

namespace composer::recording {

enum class FakeInputFault {
    none,
    disconnect,
    sampleRateChange,
    dropout
};

struct FakeRecordingReport final {
    std::size_t callbackCount{};
    std::int64_t framesPresented{};
    float peak{};
    std::uint64_t sampleHash{};
};

class FakeRecordingDevice final {
public:
    FakeRecordingDevice(double sampleRate = 48000.0, std::size_t blockSize = 256,
        std::size_t inputChannels = 2);

    [[nodiscard]] FakeRecordingReport run(RecordingSession& session,
        std::int64_t capturedFrames, FakeInputFault fault = FakeInputFault::none,
        std::int64_t faultAtPresentedFrame = -1) const;

private:
    double sampleRate_{};
    std::size_t blockSize_{};
    std::size_t inputChannels_{};
};

}  // namespace composer::recording
