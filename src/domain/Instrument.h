#pragma once

#include "domain/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace composer::domain {

// The musical role of a built-in instrument. Role drives chord-tool availability (drums are
// unpitched), default rhythm patterns, and the render timbre. Roles are stable identifiers; new
// instrument packs (S2+) register against these without changing the track model.
enum class InstrumentRole : std::uint8_t {
    keys,        // piano and other polyphonic pitched keyboards
    guitar,      // plucked, strum-friendly, mid register
    bass,        // monophonic low register, pulse-friendly
    drums,       // unpitched percussion; chord tool disabled
    synth,       // sustained polyphonic lead/pad
};

// Render timbre selects the deterministic synthesis voice the offline/realtime renderer uses for a
// track. Keeping it in the domain (rather than the audio layer) means save/reload preserves the
// exact sound and the golden-render gate stays stable across slices.
enum class RenderTimbre : std::uint8_t {
    sine,        // pure decaying sine (S1 provisional tone, retained for keys)
    pluck,       // fast-decay, bright partials (guitar)
    triangle,    // mellow, rounded low end (bass)
    noise,       // band-limited percussive burst (drums)
    saw,         // sustained, buzzy (synth)
};

// A built-in instrument definition. The registry is fixed and pure; instrumentId is the stable
// string stored on a track. lowestPitch/highestPitch bound playable notes so chord/rhythm voicings
// can be range-corrected. defaultChordOctave is the octave the chord popover opens at for the role.
struct InstrumentDef final {
    std::string_view instrumentId;
    std::string_view displayName;
    InstrumentRole role{};
    RenderTimbre timbre{};
    Pitch lowestPitch{kMinPitch};
    Pitch highestPitch{kMaxPitch};
    int defaultChordOctave{4};

    [[nodiscard]] constexpr bool isPitched() const noexcept { return role != InstrumentRole::drums; }
};

// The five built-in instruments shipped in S2. piano/bass/drums existed (as bare ids) in S1;
// guitar and synth gain real role/timbre identity here. The order is the catalog order shown in the
// instrument browser.
inline constexpr std::array<InstrumentDef, 5> kBuiltinInstruments{
    InstrumentDef{"builtin.piano", "Piano", InstrumentRole::keys, RenderTimbre::sine, 21, 108, 4},
    InstrumentDef{"builtin.guitar", "Guitar", InstrumentRole::guitar, RenderTimbre::pluck, 40, 88, 4},
    InstrumentDef{"builtin.bass", "Bass", InstrumentRole::bass, RenderTimbre::triangle, 28, 67, 2},
    InstrumentDef{"builtin.drums", "Drums", InstrumentRole::drums, RenderTimbre::noise, 35, 81, 4},
    InstrumentDef{"builtin.synth", "Synth", InstrumentRole::synth, RenderTimbre::saw, 36, 96, 4},
};

// Looks up a built-in instrument by its stable id. Returns nullopt for unknown ids (e.g. a project
// referencing a sample pack that is not installed); callers treat that as an offline instrument.
[[nodiscard]] constexpr std::optional<InstrumentDef> findInstrument(
    const std::string_view instrumentId) noexcept {
    for (const auto& def : kBuiltinInstruments) {
        if (def.instrumentId == instrumentId) {
            return def;
        }
    }
    return std::nullopt;
}

// The render timbre to use for a track's instrument id. Unknown instruments fall back to a plain
// sine so an offline/missing instrument still renders something deterministic rather than silence.
[[nodiscard]] constexpr RenderTimbre timbreFor(const std::string_view instrumentId) noexcept {
    if (const auto def = findInstrument(instrumentId)) {
        return def->timbre;
    }
    return RenderTimbre::sine;
}

}  // namespace composer::domain
