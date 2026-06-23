#include "domain/Chord.h"

#include <algorithm>

namespace composer::domain {
namespace {

using application::Error;
using application::ErrorCode;

// Semitone offsets above the root for each quality's base triad.
[[nodiscard]] std::array<int, 3> triadIntervals(const ChordQuality quality) noexcept {
    switch (quality) {
        case ChordQuality::major: return {0, 4, 7};
        case ChordQuality::minor: return {0, 3, 7};
        case ChordQuality::diminished: return {0, 3, 6};
        case ChordQuality::augmented: return {0, 4, 8};
        case ChordQuality::dominant: return {0, 4, 7};
    }
    return {0, 4, 7};
}

// Seventh degree depends on the quality: minor/dominant use the minor seventh (10),
// major uses the major seventh (11), diminished uses the diminished seventh (9).
[[nodiscard]] int seventhInterval(const ChordQuality quality) noexcept {
    switch (quality) {
        case ChordQuality::major: return 11;
        case ChordQuality::minor: return 10;
        case ChordQuality::diminished: return 9;
        case ChordQuality::augmented: return 11;
        case ChordQuality::dominant: return 10;
    }
    return 10;
}

}  // namespace

application::Result<std::vector<Pitch>> chordPitches(const ChordSpec& spec) {
    if (spec.rootPitchClass < 0 || spec.rootPitchClass > 11) {
        return Error{ErrorCode::invalidArgument, "chord root pitch class must be 0..11"};
    }
    if (!isValidVelocity(spec.velocity)) {
        return Error{ErrorCode::invalidArgument, "chord velocity must be 1..127"};
    }

    std::vector<int> intervals;
    intervals.reserve(4);
    for (const int interval : triadIntervals(spec.quality)) {
        intervals.push_back(interval);
    }
    if (spec.extension == ChordExtension::seventh || spec.extension == ChordExtension::ninth) {
        intervals.push_back(seventhInterval(spec.quality));
    }
    if (spec.extension == ChordExtension::ninth) {
        intervals.push_back(14);  // compound major second
    }

    if (spec.inversion < 0 || spec.inversion >= static_cast<int>(intervals.size())) {
        return Error{ErrorCode::invalidArgument, "chord inversion is out of range for this voicing"};
    }

    // MIDI 60 == C4. Root = 12 * (octave + 1) + pitchClass.
    const int rootMidi = 12 * (spec.octave + 1) + spec.rootPitchClass;

    std::vector<Pitch> pitches;
    pitches.reserve(intervals.size());
    for (int index = 0; index < static_cast<int>(intervals.size()); ++index) {
        int semitone = rootMidi + intervals[static_cast<std::size_t>(index)];
        // Inversion: raise the lowest `inversion` voices by an octave.
        if (index < spec.inversion) {
            semitone += 12;
        }
        if (!isValidPitch(semitone)) {
            return Error{ErrorCode::invalidArgument,
                "chord voice falls outside the MIDI pitch range; lower the octave or inversion"};
        }
        pitches.push_back(static_cast<Pitch>(semitone));
    }

    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

std::string_view chordQualityName(const ChordQuality quality) noexcept {
    switch (quality) {
        case ChordQuality::major: return "major";
        case ChordQuality::minor: return "minor";
        case ChordQuality::diminished: return "diminished";
        case ChordQuality::augmented: return "augmented";
        case ChordQuality::dominant: return "dominant";
    }
    return "major";
}

std::string_view chordExtensionName(const ChordExtension extension) noexcept {
    switch (extension) {
        case ChordExtension::none: return "none";
        case ChordExtension::seventh: return "seventh";
        case ChordExtension::ninth: return "ninth";
    }
    return "none";
}

}  // namespace composer::domain
