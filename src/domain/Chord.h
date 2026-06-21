#pragma once

#include "application/Result.h"
#include "domain/Types.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace composer::domain {

// Chromatic pitch class, 0 = C. Octave 4 with root C is MIDI 60 (middle C convention used here).
enum class ChordQuality : std::uint8_t {
    major,
    minor,
    diminished,
    augmented,
    dominant,  // major triad, used as the base for dominant extensions
};

enum class ChordExtension : std::uint8_t {
    none,
    seventh,
    ninth,
};

// A chord request as the user expresses it in the popover. octave is the octave of the root note;
// rootPitchClass is 0..11. inversion rotates the lowest voices upward by an octave.
struct ChordSpec final {
    int rootPitchClass{0};  // 0 = C .. 11 = B
    int octave{4};          // C4 root -> MIDI 60
    ChordQuality quality{ChordQuality::major};
    ChordExtension extension{ChordExtension::none};
    int inversion{0};
    Velocity velocity{kMaxVelocity};
};

// Returns the chord's constituent MIDI pitches in ascending order, or a typed error if the spec
// is invalid or any voice would fall outside the MIDI range. This is the single source of chord
// truth shared by the command layer, preview, and tests.
[[nodiscard]] application::Result<std::vector<Pitch>> chordPitches(const ChordSpec& spec);

[[nodiscard]] std::string_view chordQualityName(ChordQuality quality) noexcept;
[[nodiscard]] std::string_view chordExtensionName(ChordExtension extension) noexcept;

}  // namespace composer::domain
