#include "recording/FakeRecordingDevice.h"

#include "audio/contracts/RealtimeSafety.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <vector>

namespace composer::recording {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashFloat(std::uint64_t& hash, const float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (int shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<std::uint8_t>(bits >> static_cast<unsigned int>(shift));
        hash *= kFnvPrime;
    }
}

}  // namespace

FakeRecordingDevice::FakeRecordingDevice(const double sampleRate, const std::size_t blockSize,
    const std::size_t inputChannels)
    : sampleRate_(sampleRate),
      blockSize_(std::max<std::size_t>(1, blockSize)),
      inputChannels_(std::max<std::size_t>(1, inputChannels)) {}

FakeRecordingReport FakeRecordingDevice::run(RecordingSession& session,
    const std::int64_t capturedFrames, const FakeInputFault fault,
    const std::int64_t faultAtPresentedFrame) const {
    std::vector<float> storage(blockSize_ * inputChannels_, 0.0F);
    std::vector<const float*> channels(inputChannels_, nullptr);
    for (std::size_t channel = 0; channel < inputChannels_; ++channel) {
        channels[channel] = storage.data() + channel * blockSize_;
    }
    FakeRecordingReport report{.sampleHash = kFnvOffset};
    const auto countIn = std::max<std::int64_t>(0, session.countInFramesRemaining());
    const auto totalFrames = countIn + std::max<std::int64_t>(0, capturedFrames);
    bool injected{};
    while (report.framesPresented < totalFrames
        && session.state() != RecordingState::failedSafe
        && session.state() != RecordingState::idle) {
        const auto frames = static_cast<std::size_t>(std::min<std::int64_t>(
            static_cast<std::int64_t>(blockSize_), totalFrames - report.framesPresented));
        for (std::size_t channel = 0; channel < inputChannels_; ++channel) {
            auto* output = storage.data() + channel * blockSize_;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const auto absolute = report.framesPresented + static_cast<std::int64_t>(frame);
                const auto phase = 2.0 * std::numbers::pi * (220.0 + 110.0 * channel)
                    * static_cast<double>(absolute) / sampleRate_;
                output[frame] = static_cast<float>(0.25 * std::sin(phase));
                report.peak = std::max(report.peak, std::abs(output[frame]));
                hashFloat(report.sampleHash, output[frame]);
            }
        }
        {
            audio::RealtimeSafety::Scope realtime;
            session.processInput(channels.data(), channels.size(), frames);
        }
        report.framesPresented += static_cast<std::int64_t>(frames);
        ++report.callbackCount;
        if (!injected && fault != FakeInputFault::none && faultAtPresentedFrame >= 0
            && report.framesPresented >= faultAtPresentedFrame) {
            injected = true;
            switch (fault) {
                case FakeInputFault::disconnect: session.notifyDeviceDisconnected(); break;
                case FakeInputFault::sampleRateChange: session.notifySampleRateChanged(); break;
                case FakeInputFault::dropout: session.notifyInputDropout(); break;
                case FakeInputFault::none: break;
            }
        }
    }
    return report;
}

}  // namespace composer::recording
