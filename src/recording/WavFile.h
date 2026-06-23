#pragma once

#include "application/Result.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace composer::recording {

struct WavInfo final {
    double sampleRate{};
    int channels{};
    std::int64_t frames{};
    std::uint16_t bitsPerSample{};
    bool floatingPoint{};
};

class FloatWavWriter final {
public:
    [[nodiscard]] application::Result<std::monostate> open(
        const std::filesystem::path& path, double sampleRate, int channels,
        std::uint64_t failAfterDataBytes = std::numeric_limits<std::uint64_t>::max());
    [[nodiscard]] bool append(const float* interleaved, std::size_t frames) noexcept;
    [[nodiscard]] application::Result<std::monostate> finalize();
    void closeWithoutFinalize() noexcept;

    [[nodiscard]] std::int64_t framesWritten() const noexcept { return framesWritten_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::ofstream stream_;
    std::filesystem::path path_;
    int channels_{};
    std::int64_t framesWritten_{};
    std::uint64_t dataBytesWritten_{};
    std::uint64_t failAfterDataBytes_{std::numeric_limits<std::uint64_t>::max()};
};

struct WavFile final {
    [[nodiscard]] static application::Result<WavInfo> inspect(
        const std::filesystem::path& path);
    [[nodiscard]] static application::Result<WavInfo> repairFloatHeader(
        const std::filesystem::path& path);
    [[nodiscard]] static application::Result<std::vector<float>> readMonoPeaks(
        const std::filesystem::path& path, std::size_t peakCount);
    [[nodiscard]] static application::Result<std::pair<WavInfo, std::vector<float>>>
    readFloatInterleaved(const std::filesystem::path& path);
};

}  // namespace composer::recording
