#pragma once

#include "application/Result.h"
#include "domain/Instrument.h"
#include "domain/Types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace composer::domain {

// The rhythm patterns offered by "Generate rhythm from notes". Each is a deterministic transform of
// a source pitch set into timed note events; the set determines harmony, the pattern determines
// timing. Patterns map loosely to instrument roles (DESIGN.md: sustained, block rhythm, arpeggio,
// guitar strum, bass pulse) but the user may pick any pattern for any pitched track.
enum class RhythmPattern : std::uint8_t {
    sustained,   // one hit per cycle, full length (keys/synth pad)
    block,       // repeated block chords on a steady subdivision
    arpeggio,    // pitches played one at a time, cycling low->high
    strum,       // block chord with a small per-voice time offset (guitar)
    bassPulse,   // lowest pitch only, pulsed on the subdivision (bass)
};

// A pitch + velocity to be rhythmically placed. The generator never invents harmony; it only
// reorders/repeats the pitches it is given, so chord-inserted notes stay editable and in key.
struct RhythmVoice final {
    Pitch pitch{};
    Velocity velocity{kMaxVelocity};

    [[nodiscard]] bool operator==(const RhythmVoice&) const = default;
};

// A request to generate a rhythm over a single span [start, start+length) of the clip timeline.
// subdivision is the note value of one cycle in ticks (e.g. kPpq/2 for eighth notes). seed makes
// any humanized/random aspects reproducible: the same request always yields byte-identical notes,
// which the deterministic-generation exit gate relies on.
struct RhythmRequest final {
    RhythmPattern pattern{RhythmPattern::block};
    Tick start{};
    Tick length{};
    Tick subdivision{};   // ticks per cycle; must be > 0
    std::uint64_t seed{0};
    std::vector<RhythmVoice> voices;  // source pitches (ascending or not; generator sorts as needed)
};

// A generated note placed relative to the clip timeline. These become ordinary NoteEvents when the
// preview is applied; the command layer assigns ids and inserts them in one undo step.
struct GeneratedNote final {
    Tick start{};
    Tick duration{};
    Pitch pitch{};
    Velocity velocity{};

    [[nodiscard]] Tick end() const noexcept { return start + duration; }
    [[nodiscard]] bool operator==(const GeneratedNote&) const = default;
};

// Generates the rhythm as timed notes, or a typed error if the request is malformed (no voices,
// non-positive subdivision/length). The result is deterministic in the request alone. Notes are
// returned in canonical order (start, then pitch). This is pure: it touches no project state.
[[nodiscard]] application::Result<std::vector<GeneratedNote>> generateRhythm(
    const RhythmRequest& request);

[[nodiscard]] std::string_view rhythmPatternName(RhythmPattern pattern) noexcept;

// The pattern that best fits an instrument role, used to preselect the popover default.
[[nodiscard]] RhythmPattern defaultPatternFor(InstrumentRole role) noexcept;

}  // namespace composer::domain
