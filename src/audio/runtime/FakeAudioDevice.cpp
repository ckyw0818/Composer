#include "audio/runtime/FakeAudioDevice.h"

#include "audio/contracts/RealtimeSafety.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

namespace composer::audio {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashSample(std::uint64_t& hash, const float sample) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(sample);
    for (int shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<std::uint8_t>(bits >> shift);
        hash *= kFnvPrime;
    }
}

}  // namespace

FakeAudioDevice::FakeAudioDevice(const AudioSpec spec) : spec_(spec) {}

FakeDeviceReport FakeAudioDevice::render(
    AudioRenderer& renderer,
    const domain::ProjectSample totalFrames) const {
    renderer.prepare(spec_);

    const auto blockSize = static_cast<domain::ProjectSample>(spec_.maximumBlockSize);
    const auto callbackCount = static_cast<std::size_t>((totalFrames + blockSize - 1) / blockSize);
    std::vector<float> samples(spec_.maximumBlockSize * spec_.outputChannels, 0.0F);
    std::vector<float*> channels(spec_.outputChannels, nullptr);
    std::vector<double> timings;
    timings.reserve(callbackCount);

    for (std::size_t channel = 0; channel < spec_.outputChannels; ++channel) {
        channels[channel] = samples.data() + (channel * spec_.maximumBlockSize);
    }

    FakeDeviceReport report{.sampleHash = kFnvOffset, .callbackCount = callbackCount};
    domain::ProjectSample startSample = 0;

    for (std::size_t callback = 0; callback < callbackCount; ++callback) {
        const auto remaining = totalFrames - startSample;
        const auto frames = static_cast<std::size_t>(
            std::min<domain::ProjectSample>(blockSize, remaining));
        const RenderBlock block{
            .outputs = channels.data(),
            .outputChannels = spec_.outputChannels,
            .frameCount = frames,
            .startSample = startSample};

        const auto begin = std::chrono::steady_clock::now();
        {
            RealtimeSafety::Scope realtimeScope;
            renderer.render(block);
        }
        const auto end = std::chrono::steady_clock::now();
        timings.push_back(std::chrono::duration<double, std::milli>(end - begin).count());

        for (std::size_t channel = 0; channel < spec_.outputChannels; ++channel) {
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const auto value = channels[channel][frame];
                hashSample(report.sampleHash, value);
                report.peakMagnitude = std::max(
                    report.peakMagnitude,
                    static_cast<double>(std::abs(value)));
            }
        }
        startSample += static_cast<domain::ProjectSample>(frames);
    }

    std::sort(timings.begin(), timings.end());
    const auto percentileIndex = timings.empty()
        ? 0U
        : static_cast<std::size_t>(0.99 * static_cast<double>(timings.size() - 1));
    report.p99Milliseconds = timings.empty() ? 0.0 : timings[percentileIndex];
    return report;
}

}  // namespace composer::audio
