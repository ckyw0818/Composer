#pragma once

#include "domain/Instrument.h"
#include "domain/Types.h"

#include <cstdint>
#include <vector>

namespace composer::audio {

// A single note flattened to absolute project-sample time, ready for the audio thread. The
// snapshot is immutable and self-contained: the audio callback never reads the domain model.
struct SnapshotNote final {
    domain::ProjectSample startSample{};
    domain::ProjectSample endSample{};  // half-open [start, end)
    domain::Pitch pitch{};
    float amplitude{};  // velocity scaled to 0..1
    float pan{};  // -1 left, 0 centre, +1 right
    domain::RenderTimbre timbre{domain::RenderTimbre::sine};
    std::uint32_t voiceGroup{};  // stable track index used in deterministic voice seeding
};

struct SnapshotAudioClip final {
    domain::ProjectSample startSample{};
    std::int64_t sourceOffsetFrames{};
    std::int64_t lengthFrames{};
    int channels{1};
    float gain{1.0F};
    float pan{};
    std::int64_t fadeInFrames{};
    std::int64_t fadeOutFrames{};
    std::vector<float> interleaved;
};

// Immutable render plan compiled from a domain::Project by ProjectCompiler. Notes are sorted by
// start sample so the renderer can advance a cursor without scanning the whole list each block.
struct ProjectSnapshot final {
    domain::Revision revision{};
    double sampleRate{48000.0};
    domain::ProjectSample lengthSamples{};
    std::vector<SnapshotNote> notes;
    std::vector<SnapshotAudioClip> audioClips;
};

}  // namespace composer::audio
