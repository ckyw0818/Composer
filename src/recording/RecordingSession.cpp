#include "recording/RecordingSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>
#include <system_error>
#include <variant>

namespace composer::recording {
namespace {

using application::Error;
using application::ErrorCode;

Error errorFor(const ErrorCode code) {
    switch (code) {
        case ErrorCode::diskSpaceLow:
            return {code, "recording stopped because the destination ran out of space"};
        case ErrorCode::deviceDisconnected:
            return {code, "recording device disconnected; the captured take was preserved"};
        case ErrorCode::sampleRateChanged:
            return {code, "device sample rate changed during recording; the take was preserved"};
        case ErrorCode::inputDropout:
            return {code, "audio input dropped out; the take was preserved up to the failure"};
        default:
            return {ErrorCode::audioWriteFailure,
                "recording writer could not keep a complete audio stream"};
    }
}

bool isSafeRelativePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) {
        if (part == "..") return false;
    }
    return true;
}

}  // namespace

RecordingSession::~RecordingSession() {
    if (state() != RecordingState::idle) {
        (void) stop();
    }
}

application::Result<std::monostate> RecordingSession::start(const RecordingRequest& request) {
    if (state() != RecordingState::idle) {
        return Error{ErrorCode::recordingBusy, "another recording operation is already active"};
    }
    if (request.projectRoot.empty() || request.takeId.empty() || request.trackId.empty()
        || !std::isfinite(request.sampleRate) || request.sampleRate < 8000.0
        || request.sampleRate > 384000.0 || request.channels <= 0 || request.channels > 32
        || request.firstInputChannel < 0 || !std::isfinite(request.beatsPerMinute)
        || request.beatsPerMinute <= 0.0 || request.beatsPerBar <= 0 || request.countInBars < 0
        || request.fifoCapacityFrames == 0 || request.expectedDurationSeconds < 0.0) {
        return Error{ErrorCode::invalidArgument, "recording request is invalid"};
    }

    request_ = request;
    const auto audioDirectory = request.projectRoot / "Audio";
    const auto recoveryDirectory = request.projectRoot / "Recovery";
    std::error_code fileError;
    std::filesystem::create_directories(audioDirectory, fileError);
    if (!fileError) std::filesystem::create_directories(recoveryDirectory, fileError);
    if (fileError) {
        return Error{ErrorCode::recordingPathUnavailable,
            "recording destination directories could not be created"};
    }

    const auto probePath = audioDirectory / (request.takeId + ".write-test");
    {
        std::ofstream probe{probePath, std::ios::binary | std::ios::trunc};
        probe.put('\0');
        probe.flush();
        if (!probe) {
            std::filesystem::remove(probePath, fileError);
            return Error{ErrorCode::recordingPathUnavailable,
                "recording destination is not writable"};
        }
    }
    std::filesystem::remove(probePath, fileError);
    fileError.clear();

    const auto space = std::filesystem::space(request.projectRoot, fileError);
    if (fileError) {
        return Error{ErrorCode::recordingPathUnavailable,
            "recording destination free space could not be measured"};
    }
    const long double audioBytes = request.expectedDurationSeconds * request.sampleRate
        * static_cast<double>(request.channels) * 4.0;
    const auto requiredBytes = request.minimumFreeBytes
        + static_cast<std::uintmax_t>(std::max<long double>(0.0L, audioBytes));
    if (space.available < requiredBytes) {
        return Error{ErrorCode::diskSpaceLow,
            "recording destination does not have enough free space for the requested take"};
    }

    partialPath_ = audioDirectory / (request.takeId + ".partial.wav");
    finalPath_ = audioDirectory / (request.takeId + ".wav");
    journalPath_ = recoveryDirectory / (request.takeId + ".journal");
    std::filesystem::remove(partialPath_, fileError);
    std::filesystem::remove(finalPath_, fileError);

    fifo_ = std::make_unique<AudioFifo>(request.fifoCapacityFrames,
        static_cast<std::size_t>(request.channels));
    writerScratch_.assign(std::min<std::size_t>(request.fifoCapacityFrames, 4096)
            * static_cast<std::size_t>(request.channels),
        0.0F);
    writer_ = std::make_unique<FloatWavWriter>();
    auto opened = writer_->open(
        partialPath_, request.sampleRate, request.channels, request.failAfterDataBytes);
    if (std::holds_alternative<Error>(opened)) {
        writer_.reset();
        fifo_.reset();
        return std::get<Error>(opened);
    }

    journal_ = RecoveryJournalEntry{
        request.takeId,
        request.trackId,
        std::filesystem::path{"Audio"} / partialPath_.filename(),
        std::filesystem::path{"Audio"} / finalPath_.filename(),
        request.startSample,
        request.sampleRate,
        request.channels,
        JournalStatus::recording,
        0,
        ErrorCode::audioWriteFailure};
    auto journalResult = RecoveryJournal::save(journalPath_, journal_);
    if (std::holds_alternative<Error>(journalResult)) {
        writer_->closeWithoutFinalize();
        std::filesystem::remove(partialPath_, fileError);
        writer_.reset();
        fifo_.reset();
        return std::get<Error>(journalResult);
    }

    stopRequested_.store(false, std::memory_order_relaxed);
    failureCode_.store(-1, std::memory_order_relaxed);
    dropoutCount_.store(0, std::memory_order_relaxed);
    inputPeak_.store(0.0F, std::memory_order_relaxed);
    inputClipped_.store(false, std::memory_order_relaxed);
    const auto countInFrames = static_cast<std::int64_t>(std::llround(
        static_cast<double>(request.countInBars * request.beatsPerBar) * 60.0
        / request.beatsPerMinute * request.sampleRate));
    countInFramesRemaining_.store(countInFrames, std::memory_order_relaxed);
    state_.store(countInFrames > 0 ? RecordingState::countIn : RecordingState::recording,
        std::memory_order_release);
    writerThread_ = std::thread{[this] { writerLoop(); }};
    return std::monostate{};
}

void RecordingSession::processInput(const float* const* inputs,
    const std::size_t availableInputChannels, const std::size_t frameCount) noexcept {
    float peak = 0.0F;
    if (inputs != nullptr && availableInputChannels > 0) {
        const auto first = static_cast<std::size_t>(std::max(0, request_.firstInputChannel));
        const auto channelEnd = std::min(availableInputChannels,
            first + static_cast<std::size_t>(std::max(1, request_.channels)));
        for (std::size_t channel = first; channel < channelEnd; ++channel) {
            if (inputs[channel] == nullptr) continue;
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                peak = std::max(peak, std::abs(inputs[channel][frame]));
            }
        }
    }
    inputPeak_.store(peak, std::memory_order_relaxed);
    if (peak >= 0.999F) inputClipped_.store(true, std::memory_order_relaxed);

    auto current = state_.load(std::memory_order_acquire);
    std::size_t inputOffset{};
    if (current == RecordingState::countIn) {
        const auto remaining = countInFramesRemaining_.load(std::memory_order_relaxed);
        if (remaining > static_cast<std::int64_t>(frameCount)) {
            countInFramesRemaining_.store(
                remaining - static_cast<std::int64_t>(frameCount), std::memory_order_relaxed);
            return;
        }
        inputOffset = static_cast<std::size_t>(std::max<std::int64_t>(0, remaining));
        countInFramesRemaining_.store(0, std::memory_order_relaxed);
        state_.store(RecordingState::recording, std::memory_order_release);
        current = RecordingState::recording;
    }
    if (current != RecordingState::recording || inputOffset >= frameCount || fifo_ == nullptr) {
        return;
    }
    const auto frames = frameCount - inputOffset;
    if (!fifo_->push(inputs, availableInputChannels,
            static_cast<std::size_t>(request_.firstInputChannel), inputOffset, frames)) {
        fail(ErrorCode::inputDropout);
    }
}

void RecordingSession::notifyDeviceDisconnected() noexcept { fail(ErrorCode::deviceDisconnected); }
void RecordingSession::notifySampleRateChanged() noexcept { fail(ErrorCode::sampleRateChanged); }
void RecordingSession::notifyInputDropout() noexcept {
    dropoutCount_.fetch_add(1, std::memory_order_relaxed);
    fail(ErrorCode::inputDropout);
}

void RecordingSession::fail(const ErrorCode code) noexcept {
    int expected = -1;
    failureCode_.compare_exchange_strong(expected, static_cast<int>(code),
        std::memory_order_relaxed, std::memory_order_relaxed);
    state_.store(RecordingState::failedSafe, std::memory_order_release);
    stopRequested_.store(true, std::memory_order_release);
}

void RecordingSession::writerLoop() noexcept {
    const auto scratchFrames = writerScratch_.size() / static_cast<std::size_t>(request_.channels);
    std::int64_t nextJournalFrame = static_cast<std::int64_t>(request_.sampleRate);
    while (!stopRequested_.load(std::memory_order_acquire)
        || (fifo_ != nullptr && fifo_->availableFrames() > 0)) {
        const auto frames = fifo_ == nullptr ? 0 : fifo_->pop(writerScratch_.data(), scratchFrames);
        if (frames == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            continue;
        }
        if (writer_ == nullptr || !writer_->append(writerScratch_.data(), frames)) {
            fail(request_.failAfterDataBytes == std::numeric_limits<std::uint64_t>::max()
                    ? ErrorCode::audioWriteFailure
                    : ErrorCode::diskSpaceLow);
            break;
        }
        if (writer_->framesWritten() >= nextJournalFrame) {
            journal_.framesWritten = writer_->framesWritten();
            const auto saved = RecoveryJournal::save(journalPath_, journal_);
            if (std::holds_alternative<Error>(saved)) {
                fail(ErrorCode::audioWriteFailure);
                break;
            }
            nextJournalFrame += static_cast<std::int64_t>(request_.sampleRate);
        }
    }
}

void RecordingSession::joinWriter() {
    if (writerThread_.joinable()) writerThread_.join();
}

std::optional<application::Error> RecordingSession::failure() const {
    const int code = failureCode_.load(std::memory_order_relaxed);
    return code < 0 ? std::optional<Error>{}
                    : std::optional<Error>{errorFor(static_cast<ErrorCode>(code))};
}

application::Result<RecordingOutcome> RecordingSession::stop() {
    if (state() == RecordingState::idle || writer_ == nullptr || fifo_ == nullptr) {
        return Error{ErrorCode::recordingBusy, "no recording operation is active"};
    }
    state_.store(RecordingState::finalizing, std::memory_order_release);
    stopRequested_.store(true, std::memory_order_release);
    joinWriter();

    auto finalization = writer_->finalize();
    if (std::holds_alternative<Error>(finalization) && !failure().has_value()) {
        failureCode_.store(static_cast<int>(ErrorCode::audioWriteFailure),
            std::memory_order_relaxed);
    }
    const auto frames = writer_->framesWritten();
    std::error_code fileError;
    if (frames > 0 && std::filesystem::is_regular_file(partialPath_, fileError)) {
        fileError.clear();
        std::filesystem::rename(partialPath_, finalPath_, fileError);
        if (fileError && !failure().has_value()) {
            failureCode_.store(static_cast<int>(ErrorCode::audioWriteFailure),
                std::memory_order_relaxed);
        }
    }

    journal_.framesWritten = frames;
    journal_.status = JournalStatus::finalized;
    if (const auto currentFailure = failure(); currentFailure.has_value()) {
        journal_.failure = currentFailure->code;
    }
    (void) RecoveryJournal::save(journalPath_, journal_);
    if (frames > 0 && !fileError) std::filesystem::remove(journalPath_, fileError);

    RecordingOutcome outcome;
    outcome.take = RecordedTake{request_.takeId, request_.trackId, finalPath_,
        (std::filesystem::path{"Audio"} / finalPath_.filename()).generic_string(),
        request_.startSample, frames, request_.sampleRate, request_.channels, false};
    outcome.failure = failure();
    outcome.fifoHighWaterFrames = fifo_->highWaterFrames();
    outcome.dropoutCount = dropoutCount_.load(std::memory_order_relaxed);

    writer_.reset();
    fifo_.reset();
    writerScratch_.clear();
    state_.store(RecordingState::idle, std::memory_order_release);
    if (frames <= 0) {
        return outcome.failure.value_or(
            Error{ErrorCode::audioWriteFailure, "recording produced no recoverable audio frames"});
    }
    return outcome;
}

void RecordingSession::simulateCrash() {
    if (state() == RecordingState::idle) return;
    stopRequested_.store(true, std::memory_order_release);
    joinWriter();
    if (writer_ != nullptr) writer_->closeWithoutFinalize();
    writer_.reset();
    fifo_.reset();
    writerScratch_.clear();
    state_.store(RecordingState::idle, std::memory_order_release);
}

application::Result<std::vector<RecordedTake>> RecordingSession::recoverPending(
    const std::filesystem::path& projectRoot) {
    std::vector<RecordedTake> recovered;
    const auto journals = RecoveryJournal::pending(projectRoot / "Recovery");
    for (const auto& journalPath : journals) {
        auto loaded = RecoveryJournal::load(journalPath);
        if (std::holds_alternative<Error>(loaded)) return std::get<Error>(loaded);
        auto entry = std::get<RecoveryJournalEntry>(std::move(loaded));
        if (!isSafeRelativePath(entry.partialPath) || !isSafeRelativePath(entry.finalPath)) {
            return Error{ErrorCode::recoveryFailure,
                "recording journal contains a path outside the project"};
        }
        const auto partial = projectRoot / entry.partialPath;
        const auto final = projectRoot / entry.finalPath;
        application::Result<WavInfo> info = std::filesystem::exists(partial)
            ? WavFile::repairFloatHeader(partial)
            : WavFile::inspect(final);
        if (std::holds_alternative<Error>(info)) return std::get<Error>(info);
        const auto wav = std::get<WavInfo>(info);
        if (wav.frames <= 0) {
            return Error{ErrorCode::recoveryFailure, "pending recording contains no audio frames"};
        }
        std::error_code fileError;
        if (std::filesystem::exists(partial)) {
            std::filesystem::rename(partial, final, fileError);
            if (fileError) {
                return Error{ErrorCode::recoveryFailure,
                    "repaired recording could not be moved into the Audio directory"};
            }
        }
        entry.status = JournalStatus::recovered;
        entry.framesWritten = wav.frames;
        (void) RecoveryJournal::save(journalPath, entry);
        std::filesystem::remove(journalPath, fileError);
        recovered.push_back({entry.takeId, entry.trackId, final,
            entry.finalPath.generic_string(), entry.startSample, wav.frames,
            wav.sampleRate, wav.channels, true});
    }
    return recovered;
}

application::Result<std::int64_t> LatencyEstimator::estimateRoundTripSamples(
    const std::vector<float>& reference, const std::vector<float>& captured,
    const std::size_t maximumLagSamples) {
    if (reference.empty() || captured.size() < reference.size()) {
        return Error{ErrorCode::invalidArgument,
            "latency measurement needs a non-empty reference and longer capture"};
    }
    const auto maximumLag = std::min(maximumLagSamples, captured.size() - reference.size());
    double bestScore = -1.0;
    std::size_t bestLag{};
    for (std::size_t lag = 0; lag <= maximumLag; ++lag) {
        double dot{};
        double referenceEnergy{};
        double captureEnergy{};
        for (std::size_t index = 0; index < reference.size(); ++index) {
            const double left = reference[index];
            const double right = captured[lag + index];
            dot += left * right;
            referenceEnergy += left * left;
            captureEnergy += right * right;
        }
        const double denominator = std::sqrt(referenceEnergy * captureEnergy);
        const double score = denominator > 0.0 ? dot / denominator : 0.0;
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    if (bestScore < 0.5) {
        return Error{ErrorCode::deviceUnavailable,
            "latency calibration signal was not detected in the captured input"};
    }
    return static_cast<std::int64_t>(bestLag);
}

}  // namespace composer::recording
