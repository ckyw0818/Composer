#pragma once

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
    std::uint32_t voiceGroup{};  // track index, lets the renderer detune per instrument role
};

// Immutable render plan compiled from a domain::Project by ProjectCompiler. Notes are sorted by
// start sample so the renderer can advance a cursor without scanning the whole list each block.
struct ProjectSnapshot final {
    domain::Revision revision{};
    double sampleRate{48000.0};
    domain::ProjectSample lengthSamples{};
    std::vector<SnapshotNote> notes;
};

}  // namespace composer::audio
