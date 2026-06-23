#include "audio/runtime/CanonicalVerse.h"

#include "audio/runtime/ProjectCompiler.h"
#include "commands/ProjectEditor.h"
#include "domain/Chord.h"
#include "domain/IdSource.h"

#include <array>
#include <variant>

namespace composer::audio {
namespace {

using composer::commands::ProjectEditor;
using composer::domain::ChordExtension;
using composer::domain::ChordQuality;
using composer::domain::ChordSpec;
using composer::domain::EntityId;
using composer::domain::HalfOpenTickRange;
using composer::domain::kPpq;
using composer::domain::Project;
using composer::domain::SequentialIdSource;
using composer::domain::Tick;

constexpr Tick kTicksPerBeat = kPpq;
constexpr Tick kTicksPerBar = kTicksPerBeat * CanonicalVerse::beatsPerBar;
constexpr Tick kVerseTicks = kTicksPerBar * CanonicalVerse::bars;

// I - V - vi - IV in C, one chord per bar, repeated across the 8 bars.
struct BarChord final {
    int rootPitchClass;
    ChordQuality quality;
};

constexpr std::array<BarChord, 4> kProgression{
    BarChord{0, ChordQuality::major},   // C
    BarChord{7, ChordQuality::major},   // G
    BarChord{9, ChordQuality::minor},   // Am
    BarChord{5, ChordQuality::major}};  // F

template <typename T>
[[nodiscard]] T expectValue(application::Result<T> result) {
    return std::get<T>(std::move(result));
}

}  // namespace

Project CanonicalVerse::makeProject() {
    SequentialIdSource ids{"verse"};
    Project seed;
    seed.id = ids.next();
    seed.name = "Canonical Verse";
    seed.tempoMap = domain::TempoMap{static_cast<double>(tempoBpm), sampleRate};

    ProjectEditor editor{seed, ids};

    const auto piano = expectValue(editor.addInstrumentTrack("Piano", "builtin.piano"));
    const auto guitar = expectValue(editor.addInstrumentTrack("Guitar", "builtin.guitar"));
    const auto bass = expectValue(editor.addInstrumentTrack("Bass", "builtin.bass"));
    const auto drums = expectValue(editor.addInstrumentTrack("Drums", "builtin.drums"));

    const HalfOpenTickRange verseRange{0, kVerseTicks};
    const auto pianoClip = expectValue(editor.addMidiClip(piano, "Verse chords", verseRange));
    const auto guitarClip = expectValue(editor.addMidiClip(guitar, "Verse comp", verseRange));
    const auto bassClip = expectValue(editor.addMidiClip(bass, "Verse bass", verseRange));
    const auto drumClip = expectValue(editor.addMidiClip(drums, "Verse beat", verseRange));

    for (int bar = 0; bar < bars; ++bar) {
        const Tick barStart = static_cast<Tick>(bar) * kTicksPerBar;
        const BarChord& chord = kProgression[static_cast<std::size_t>(bar % 4)];

        // Piano: one whole-bar chord via the chord-insertion command (one undo step per bar).
        ChordSpec spec;
        spec.rootPitchClass = chord.rootPitchClass;
        spec.octave = 4;
        spec.quality = chord.quality;
        spec.extension = ChordExtension::none;
        spec.velocity = 96;
        std::ignore = expectValue(
            editor.insertChord(piano, pianoClip, barStart, kTicksPerBar, spec));

        // Guitar: generate a deterministic two-beat strum from one full-bar source chord.
        ChordSpec guitarSpec = spec;
        guitarSpec.octave = 5;
        guitarSpec.velocity = 80;
        const auto guitarSource = expectValue(
            editor.insertChord(guitar, guitarClip, barStart, kTicksPerBar, guitarSpec));
        ProjectEditor::RhythmApply guitarRhythm;
        guitarRhythm.pattern = domain::RhythmPattern::strum;
        guitarRhythm.subdivision = kTicksPerBeat * 2;
        guitarRhythm.seed = static_cast<std::uint64_t>(bar + 1);
        guitarRhythm.expectedRevision = editor.revision();
        guitarRhythm.sourceNoteIds = guitarSource;
        std::ignore = expectValue(editor.applyRhythm(guitar, guitarClip, guitarRhythm));

        // Bass: generate one root pulse per beat from a sustained source note.
        const domain::Pitch bassRoot = static_cast<domain::Pitch>(36 + chord.rootPitchClass);
        const auto bassSource = expectValue(
            editor.addNote(bass, bassClip, barStart, kTicksPerBar, bassRoot, 100));
        ProjectEditor::RhythmApply bassRhythm;
        bassRhythm.pattern = domain::RhythmPattern::bassPulse;
        bassRhythm.subdivision = kTicksPerBeat;
        bassRhythm.seed = static_cast<std::uint64_t>(bar + 101);
        bassRhythm.expectedRevision = editor.revision();
        bassRhythm.sourceNoteIds = {bassSource};
        std::ignore = expectValue(editor.applyRhythm(bass, bassClip, bassRhythm));

        // Drums: a steady pulse on every beat (single percussion pitch for S1).
        for (int beat = 0; beat < beatsPerBar; ++beat) {
            const Tick noteStart = barStart + static_cast<Tick>(beat) * kTicksPerBeat;
            std::ignore = expectValue(editor.addNote(
                drums, drumClip, noteStart, kTicksPerBeat / 4, 60, 110));
        }
    }

    return editor.project();
}

ProjectSnapshot CanonicalVerse::makeProjectSnapshot() {
    return ProjectCompiler::compile(makeProject(), 1);
}

RuntimeSnapshot CanonicalVerse::makeSnapshot() noexcept {
    constexpr std::int64_t samplesPerBeat = 24000;
    constexpr std::int64_t totalBeats = bars * beatsPerBar;

    return RuntimeSnapshot{
        .revision = 1,
        .lengthSamples = samplesPerBeat * totalBeats,
        .voices = {
            ClickVoice{samplesPerBeat * 2, 0, 96, 0.10F},
            ClickVoice{samplesPerBeat, samplesPerBeat / 2, 72, 0.06F},
            ClickVoice{samplesPerBeat * 2, 0, 128, 0.08F},
            ClickVoice{samplesPerBeat, 0, 48, 0.12F},
        }};
}

}  // namespace composer::audio
