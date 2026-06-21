#pragma once

#include "audio/contracts/AudioRenderer.h"
#include "audio/contracts/ProjectSnapshot.h"
#include "audio/runtime/ProjectRenderer.h"
#include "domain/Project.h"

#include <atomic>
#include <memory>

namespace composer::audio {

// Bridges the message-thread project model to the audio callback for S1 playback.
//
// Transport state (playing, playhead, loop) is atomic so the callback reads it without locking.
// When the project is edited, the UI thread compiles a fresh ProjectSnapshot and publishes a new
// ProjectRenderer through an atomic<shared_ptr>; the callback loads the current renderer with a
// lock-free atomic load and never blocks or allocates.
class PlaybackEngine final {
public:
    PlaybackEngine();

    // Message thread: recompile and publish a new render plan for the given project.
    void setProject(const domain::Project& project, domain::Revision revision);

    // Message thread: transport controls.
    void play() noexcept { playing_.store(true, std::memory_order_relaxed); }
    void stop() noexcept { playing_.store(false, std::memory_order_relaxed); }
    void togglePlay() noexcept;
    void setLooping(const bool looping) noexcept {
        looping_.store(looping, std::memory_order_relaxed);
    }
    void setPlayheadSamples(domain::ProjectSample sample) noexcept;
    void rewind() noexcept { setPlayheadSamples(0); }

    [[nodiscard]] bool isPlaying() const noexcept {
        return playing_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool isLooping() const noexcept {
        return looping_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] domain::ProjectSample playheadSamples() const noexcept {
        return playhead_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] domain::ProjectSample lengthSamples() const noexcept {
        return length_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] double sampleRate() const noexcept {
        return sampleRate_.load(std::memory_order_relaxed);
    }

    // Audio thread.
    void prepare(const AudioSpec& spec);
    void process(const RenderBlock& block) noexcept;

private:
    std::atomic<std::shared_ptr<ProjectRenderer>> renderer_;
    AudioSpec spec_{};

    std::atomic<bool> playing_{false};
    std::atomic<bool> looping_{true};
    std::atomic<domain::ProjectSample> playhead_{0};
    std::atomic<domain::ProjectSample> length_{0};
    std::atomic<double> sampleRate_{48000.0};
};

}  // namespace composer::audio
