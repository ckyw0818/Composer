#pragma once

#include "application/Result.h"
#include "domain/Project.h"

#include <cstdint>
#include <string>
#include <vector>

namespace composer::persistence {

// Standard MIDI File (SMF) import/export for the project's instrument tracks. This is the S2
// interoperability escape hatch: a multi-track project round-trips through any external DAW.
//
// The writer emits a format-1 file at the project's PPQ (domain::kPpq) so musical tick positions
// are preserved exactly. One MIDI track per instrument track; notes become note-on/note-off pairs.
// The reader is the inverse: it reconstructs instrument tracks, clips, and notes from the same
// byte layout. Round-trip is exact for the note data Composer owns (pitch, start, duration,
// velocity, track grouping); MIDI-specific channel/program metadata it does not model is ignored
// on read and defaulted on write.
//
// The parser is bounded and strict: it rejects malformed headers, truncated events, and oversized
// counts rather than allocating unboundedly, matching the security review's "bounded readers".
struct MidiFile final {
    // Serializes the project to SMF bytes. Deterministic: the same project always yields the same
    // bytes, so the round-trip gate can hash-compare.
    [[nodiscard]] static application::Result<std::vector<std::uint8_t>> exportBytes(
        const domain::Project& project);

    // Parses SMF bytes back into a project. The resulting project has one instrument track per MIDI
    // track that contains notes, a single clip per track spanning its notes, and reconstructed
    // NoteEvents. Entity ids are assigned deterministically ("midi-<n>") so two imports of the same
    // bytes compare equal. Returns a typed error for malformed input.
    [[nodiscard]] static application::Result<domain::Project> importBytes(
        const std::vector<std::uint8_t>& bytes);
};

}  // namespace composer::persistence
