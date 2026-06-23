#pragma once

#include "application/Result.h"
#include "domain/Types.h"
#include "recording/AudioFifo.h"
#include "recording/RecoveryJournal.h"
#include "recording/WavFile.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace composer::recording {

enum class RecordingState : std::uint8_t {
    idle,
    countIn,
    recording,
    finalizing,
    failedSafe
};

struct RecordingRequest final {
    std::filesystem::path projectRoot;
    std::string takeId;
    domain::EntityId trackId;
    domain::ProjectSample startSample{};
    double sampleRate{48000.0};
    int channels{1};
    int firstInputChannel{};
    double beatsPerMinute{120.0};
    int beatsPerBar{4};
    int countInBars{1};
    std::size_t fifoCapacityFrames{48000};
    std::uintmax_t minimumFreeBytes{16ULL * 1024ULL * 1024ULL};
    double expectedDurationSeconds{60.0};
    std::uint64_t failAfterDataBytes{std::numeric_limits<std::uint64_t>::max()};
};

struct RecordedTake final {
    std::string takeId;
    domain::EntityId trackId;
    std::filesystem::path absolutePath;
    std::string assetPath;
    domain::ProjectSample startSample{};
    std::int64_t frames{};
    double sampleRate{};
    int channels{};
    bool recovered{};
};

struct RecordingOutcome final {
    RecordedTake take;
    std::optional<application::Error> failure;
    std::size_t fifoHighWaterFrames{};
    std::uint64_t dropoutCount{};
};

class RecordingSession final {
public:
    RecordingSession() = default;
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    [[nodiscard]] application::Result<std::monostate> start(const RecordingRequest& request);

    // Audio callback. Safe to call with no active recording so device meter UI remains live.
    void processInput(const float* const* inputs, std::size_t availableInputChannels,
        std::size_t frameCount) noexcept;
    void notifyDeviceDisconnected() noexcept;
    void notifySampleRateChanged() noexcept;
    void notifyInputDropout() noexcept;

    [[nodiscard]] application::Result<RecordingOutcome> stop();

    // Test/crash-shutdown hook: closes the stream without header repair or journal removal.
    void simulateCrash();

    [[nodiscard]] RecordingState state() const noexcept {
        return state_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float inputPeak() const noexcept {
        return inputPeak_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool inputClipped() const noexcept {
        return inputClipped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::int64_t countInFramesRemaining() const noexcept {
        return countInFramesRemaining_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static application::Result<std::vector<RecordedTake>> recoverPending(
        const std::filesystem::path& projectRoot);

private:
    void writerLoop() noexcept;
    void fail(application::ErrorCode code) noexcept;
    [[nodiscard]] std::optional<application::Error> failure() const;
    void joinWriter();

    RecordingRequest request_;
    RecoveryJournalEntry journal_;
    std::filesystem::path journalPath_;
    std::filesystem::path partialPath_;
    std::filesystem::path finalPath_;
    std::unique_ptr<AudioFifo> fifo_;
    std::unique_ptr<FloatWavWriter> writer_;
    std::vector<float> writerScratch_;
    std::thread writerThread_;
    std::atomic<RecordingState> state_{RecordingState::idle};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> failureCode_{-1};
    std::atomic<std::int64_t> countInFramesRemaining_{0};
    std::atomic<float> inputPeak_{0.0F};
    std::atomic<bool> inputClipped_{false};
    std::atomic<std::uint64_t> dropoutCount_{0};
};

struct LatencyEstimator final {
    [[nodiscard]] static application::Result<std::int64_t> estimateRoundTripSamples(
        const std::vector<float>& reference, const std::vector<float>& captured,
        std::size_t maximumLagSamples);
};

}  // namespace composer::recording
