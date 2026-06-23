#pragma once

#include "audio/contracts/ProjectSnapshot.h"
#include "audio/contracts/RuntimeSnapshot.h"
#include "domain/Project.h"

#include <array>
#include <string_view>

namespace composer::audio {

// The canonical 8-bar verse fixture. S2 arranges the S1 chord notes through deterministic guitar
// strum and bass-pulse commands, then renders each built-in role with its own timbre.
struct CanonicalVerse final {
    static constexpr int bars = 8;
    static constexpr int beatsPerBar = 4;
    static constexpr int tempoBpm = 120;
    static constexpr double sampleRate = 48000.0;
    // The acceptance fixture is the canonical four-instrument verse (DESIGN.md: "4 instruments").
    static constexpr std::array<std::string_view, 4> trackNames{
        "Piano", "Guitar", "Bass", "Drums"};

    // Builds the canonical verse project deterministically (SequentialIdSource), exercising the
    // S1/S2 command surface: tracks, clips, chords, ordinary notes, and rhythm arrangement.
    [[nodiscard]] static domain::Project makeProject();

    // Compiles makeProject() into the renderable snapshot.
    [[nodiscard]] static ProjectSnapshot makeProjectSnapshot();

    // S0-compatible click snapshot, retained so the S0 foundation gate stays green.
    [[nodiscard]] static RuntimeSnapshot makeSnapshot() noexcept;
};

}  // namespace composer::audio
