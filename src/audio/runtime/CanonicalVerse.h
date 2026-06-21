#pragma once

#include "audio/contracts/ProjectSnapshot.h"
#include "audio/contracts/RuntimeSnapshot.h"
#include "domain/Project.h"

#include <array>
#include <string_view>

namespace composer::audio {

// The canonical 8-bar verse fixture. S0 shipped a click-track RuntimeSnapshot; S1 builds the same
// verse from a real domain::Project (instrument tracks, MIDI clips, chord-inserted notes) through
// the command layer, then compiles it to a ProjectSnapshot. The exit gate renders this fixture,
// saves and reloads its project, and checks the round-trip and render are deterministic.
struct CanonicalVerse final {
    static constexpr int bars = 8;
    static constexpr int beatsPerBar = 4;
    static constexpr int tempoBpm = 120;
    static constexpr double sampleRate = 48000.0;
    // The acceptance fixture is the canonical four-instrument verse (DESIGN.md: "4 instruments").
    // In S1 the guitar is a pitched MIDI track rendered with the same provisional tone as the
    // others; its sample-based built-in voice and rhythm generation arrive in S2.
    static constexpr std::array<std::string_view, 4> trackNames{
        "Piano", "Guitar", "Bass", "Drums"};

    // Builds the canonical verse project deterministically (SequentialIdSource), exercising the
    // S1 command surface: add tracks, add clips, insert chords, add melody/bass notes.
    [[nodiscard]] static domain::Project makeProject();

    // Compiles makeProject() into the renderable snapshot.
    [[nodiscard]] static ProjectSnapshot makeProjectSnapshot();

    // S0-compatible click snapshot, retained so the S0 foundation gate stays green.
    [[nodiscard]] static RuntimeSnapshot makeSnapshot() noexcept;
};

}  // namespace composer::audio
