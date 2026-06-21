#pragma once

#include "audio/contracts/AudioRenderer.h"
#include "audio/contracts/ProjectSnapshot.h"

#include <array>
#include <cstddef>

namespace composer::audio {

// Realtime renderer for a compiled ProjectSnapshot. Synthesizes each note as a decaying sine with
// a fixed, preallocated voice pool so render() performs zero allocation, locking, or file I/O.
//
// The snapshot is moved in at construction (off the audio thread). render() walks a note cursor
// and a bounded set of active voices; both are members sized at construction, never grown in the
// callback.
class ProjectRenderer final : public AudioRenderer {
public:
    static constexpr std::size_t kMaxVoices = 64;

    explicit ProjectRenderer(ProjectSnapshot snapshot) noexcept;

    void prepare(const AudioSpec& spec) override;
    void render(const RenderBlock& block) noexcept override;

    [[nodiscard]] const ProjectSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    struct Voice final {
        bool active{false};
        domain::ProjectSample endSample{};
        double phase{};
        double phaseIncrement{};
        float amplitude{};
    };

    void startDueNotes(domain::ProjectSample sample) noexcept;
    [[nodiscard]] float renderSample(domain::ProjectSample sample) noexcept;

    ProjectSnapshot snapshot_{};
    AudioSpec spec_{};
    std::array<Voice, kMaxVoices> voices_{};
    std::size_t cursor_{0};  // index of next note to activate, snapshot_.notes is start-sorted
};

}  // namespace composer::audio
