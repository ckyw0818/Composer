#include "domain/Rhythm.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace composer::domain {
namespace {

using application::Error;
using application::ErrorCode;

// Small, dependency-free deterministic PRNG (SplitMix64). Used only for bounded humanization
// (velocity jitter, strum spread) so the same seed always reproduces the same notes. The audio
// thread never runs this; generation happens off the message thread before Apply.
class SplitMix64 final {
public:
    explicit SplitMix64(const std::uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Signed jitter in [-magnitude, +magnitude].
    [[nodiscard]] int jitter(const int magnitude) noexcept {
        if (magnitude <= 0) {
            return 0;
        }
        const int span = 2 * magnitude + 1;
        return static_cast<int>(next() % static_cast<std::uint64_t>(span)) - magnitude;
    }

private:
    std::uint64_t state_{};
};

[[nodiscard]] Velocity clampVelocity(const int value) noexcept {
    return static_cast<Velocity>(std::clamp(value, kMinVelocity, kMaxVelocity));
}

// A small gap so consecutive same-pitch hits retrigger instead of fusing into one long note.
constexpr Tick kRetriggerGap = 10;

void appendNote(std::vector<GeneratedNote>& out, const Tick start, const Tick duration,
    const Pitch pitch, const Velocity velocity) {
    if (duration <= 0) {
        return;
    }
    out.push_back(GeneratedNote{start, duration, pitch, velocity});
}

void canonicalize(std::vector<GeneratedNote>& notes) {
    std::sort(notes.begin(), notes.end(), [](const GeneratedNote& lhs, const GeneratedNote& rhs) {
        if (lhs.start != rhs.start) {
            return lhs.start < rhs.start;
        }
        return lhs.pitch < rhs.pitch;
    });
}

[[nodiscard]] Tick retriggeredDuration(const Tick cellLength) noexcept {
    return std::max<Tick>(1, cellLength - std::min(kRetriggerGap, cellLength / 4));
}

}  // namespace

application::Result<std::vector<GeneratedNote>> generateRhythm(const RhythmRequest& request) {
    if (request.voices.empty()) {
        return Error{ErrorCode::invalidArgument, "rhythm generation needs at least one source note"};
    }
    if (request.length <= 0) {
        return Error{ErrorCode::invalidArgument, "rhythm span length must be positive"};
    }
    if (request.start < 0 || request.length > std::numeric_limits<Tick>::max() - request.start) {
        return Error{ErrorCode::invalidArgument, "rhythm span is outside the project timeline"};
    }
    if (request.subdivision <= 0) {
        return Error{ErrorCode::invalidArgument, "rhythm subdivision must be positive"};
    }
    for (const auto& voice : request.voices) {
        if (!isValidPitch(voice.pitch) || !isValidVelocity(voice.velocity)) {
            return Error{ErrorCode::invalidArgument, "source voice has invalid pitch or velocity"};
        }
    }

    // Sort source voices low->high so arpeggio/strum/bassPulse have a stable, deterministic order
    // regardless of the order the caller supplied them in.
    std::vector<RhythmVoice> voices = request.voices;
    std::sort(voices.begin(), voices.end(), [](const RhythmVoice& lhs, const RhythmVoice& rhs) {
        if (lhs.pitch != rhs.pitch) {
            return lhs.pitch < rhs.pitch;
        }
        return lhs.velocity < rhs.velocity;
    });
    // Multiple selected notes at the same pitch describe one rhythm voice. Keep the loudest so
    // overlapping source notes do not create duplicate generated note-ons.
    std::vector<RhythmVoice> uniqueVoices;
    for (const auto& voice : voices) {
        if (!uniqueVoices.empty() && uniqueVoices.back().pitch == voice.pitch) {
            uniqueVoices.back().velocity = std::max(uniqueVoices.back().velocity, voice.velocity);
        } else {
            uniqueVoices.push_back(voice);
        }
    }
    voices = std::move(uniqueVoices);

    const Tick spanStart = request.start;
    const Tick spanEnd = request.start + request.length;
    const Tick step = request.subdivision;
    SplitMix64 rng{request.seed};

    std::vector<GeneratedNote> notes;

    switch (request.pattern) {
        case RhythmPattern::sustained: {
            // One full-span hit per voice. Deterministic and dead simple; the pad case.
            for (const auto& voice : voices) {
                appendNote(notes, spanStart, request.length, voice.pitch, voice.velocity);
            }
            break;
        }
        case RhythmPattern::block: {
            // Every voice on each subdivision. Velocity gets a tiny seeded accent so repeats are
            // not mechanically identical, but stay reproducible.
            for (Tick cycle = spanStart; cycle < spanEnd; cycle += step) {
                const Tick duration = retriggeredDuration(std::min(step, spanEnd - cycle));
                for (const auto& voice : voices) {
                    appendNote(notes, cycle, duration, voice.pitch,
                        clampVelocity(voice.velocity + rng.jitter(6)));
                }
            }
            break;
        }
        case RhythmPattern::arpeggio: {
            // One voice per subdivision, cycling low->high then wrapping. Each note sustains until
            // the next subdivision.
            std::size_t index = 0;
            for (Tick cycle = spanStart; cycle < spanEnd; cycle += step) {
                const Tick duration = retriggeredDuration(std::min(step, spanEnd - cycle));
                const auto& voice = voices[index % voices.size()];
                appendNote(notes, cycle, duration, voice.pitch,
                    clampVelocity(voice.velocity + rng.jitter(4)));
                ++index;
            }
            break;
        }
        case RhythmPattern::strum: {
            // Block chord per subdivision, but voices are offset slightly later as pitch rises, so
            // the chord "rolls" like a strummed guitar. The roll width is a fraction of the step.
            const Tick rollWidth = std::min<Tick>(step / 8, 30);
            for (Tick cycle = spanStart; cycle < spanEnd; cycle += step) {
                const Tick cellEnd = std::min(cycle + step, spanEnd);
                for (std::size_t v = 0; v < voices.size(); ++v) {
                    const Tick offset = rollWidth == 0
                        ? 0
                        : static_cast<Tick>(v) * rollWidth + rng.jitter(2);
                    const Tick start = std::clamp(cycle + offset, cycle, cellEnd - 1);
                    const Tick duration = retriggeredDuration(cellEnd - start);
                    appendNote(notes, start, duration, voices[v].pitch,
                        clampVelocity(voices[v].velocity + rng.jitter(5)));
                }
            }
            break;
        }
        case RhythmPattern::bassPulse: {
            // Lowest voice only, pulsed on every subdivision. The bread-and-butter bass line.
            const auto& root = voices.front();
            for (Tick cycle = spanStart; cycle < spanEnd; cycle += step) {
                const Tick duration = retriggeredDuration(std::min(step, spanEnd - cycle));
                appendNote(notes, cycle, duration, root.pitch,
                    clampVelocity(root.velocity + rng.jitter(6)));
            }
            break;
        }
    }

    canonicalize(notes);
    return notes;
}

std::string_view rhythmPatternName(const RhythmPattern pattern) noexcept {
    switch (pattern) {
        case RhythmPattern::sustained: return "sustained";
        case RhythmPattern::block: return "block";
        case RhythmPattern::arpeggio: return "arpeggio";
        case RhythmPattern::strum: return "strum";
        case RhythmPattern::bassPulse: return "bassPulse";
    }
    return "block";
}

RhythmPattern defaultPatternFor(const InstrumentRole role) noexcept {
    switch (role) {
        case InstrumentRole::keys: return RhythmPattern::block;
        case InstrumentRole::guitar: return RhythmPattern::strum;
        case InstrumentRole::bass: return RhythmPattern::bassPulse;
        case InstrumentRole::drums: return RhythmPattern::block;
        case InstrumentRole::synth: return RhythmPattern::sustained;
    }
    return RhythmPattern::block;
}

}  // namespace composer::domain
