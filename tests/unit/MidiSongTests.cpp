#include "commands/ProjectEditor.h"
#include "domain/Chord.h"
#include "domain/IdSource.h"
#include "domain/Project.h"
#include "domain/TempoMap.h"
#include "tests/TestRunner.h"

#include <variant>
#include <vector>

namespace {

using namespace composer;

template <typename T>
[[nodiscard]] bool isOk(const application::Result<T>& result) {
    return std::holds_alternative<T>(result);
}

template <typename T>
[[nodiscard]] bool isError(const application::Result<T>& result) {
    return std::holds_alternative<application::Error>(result);
}

void testTempoMap(tests::TestContext& tests) {
    const domain::TempoMap tempo{120.0, 48000.0};
    // 120 BPM -> 0.5 s/beat -> 24000 samples/beat at 48 kHz. One beat == kPpq ticks.
    tests.expect(tempo.ticksToSamples(domain::kPpq) == 24000,
        "one beat at 120 BPM/48k must be 24000 samples");
    tests.expect(tempo.ticksToSamples(0) == 0, "zero ticks must be zero samples");
    tests.expect(tempo.ticksToSamples(domain::kPpq * 4) == 96000,
        "one 4/4 bar must be 96000 samples");
    // Round trip near identity for whole beats.
    tests.expect(tempo.samplesToTicks(24000) == domain::kPpq,
        "24000 samples must convert back to one beat of ticks");
    tests.expect(domain::TempoMap{}.isValid(), "default tempo map must be valid");
    tests.expect(!domain::TempoMap(0.0, 48000.0).isValid(), "zero BPM must be invalid");
}

void testChordTheory(tests::TestContext& tests) {
    // C major triad at octave 4 -> C4 E4 G4 -> 60 64 67.
    domain::ChordSpec cMajor;
    cMajor.rootPitchClass = 0;
    cMajor.octave = 4;
    cMajor.quality = domain::ChordQuality::major;
    auto result = domain::chordPitches(cMajor);
    tests.expect(isOk(result), "C major must be valid");
    const auto& pitches = std::get<std::vector<domain::Pitch>>(result);
    tests.expect(pitches.size() == 3 && pitches[0] == 60 && pitches[1] == 64 && pitches[2] == 67,
        "C major triad must be 60 64 67");

    // A minor triad: A4 C5 E5 -> 69 72 76.
    domain::ChordSpec aMinor;
    aMinor.rootPitchClass = 9;
    aMinor.octave = 4;
    aMinor.quality = domain::ChordQuality::minor;
    const auto aMinorPitches = std::get<std::vector<domain::Pitch>>(domain::chordPitches(aMinor));
    tests.expect(aMinorPitches.size() == 3 && aMinorPitches[0] == 69 && aMinorPitches[2] == 76,
        "A minor triad must be 69 72 76");

    // Seventh extension adds a fourth voice.
    domain::ChordSpec g7 = cMajor;
    g7.rootPitchClass = 7;
    g7.quality = domain::ChordQuality::dominant;
    g7.extension = domain::ChordExtension::seventh;
    tests.expect(std::get<std::vector<domain::Pitch>>(domain::chordPitches(g7)).size() == 4,
        "dominant seventh must have four voices");

    // Out-of-range octave must fail rather than wrap.
    domain::ChordSpec tooHigh = cMajor;
    tooHigh.octave = 10;
    tests.expect(isError(domain::chordPitches(tooHigh)),
        "chord above MIDI range must be rejected");

    // Invalid inversion must fail.
    domain::ChordSpec badInversion = cMajor;
    badInversion.inversion = 9;
    tests.expect(isError(domain::chordPitches(badInversion)),
        "inversion past voice count must be rejected");
}

void testTrackAndNoteCommands(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"t"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};

    const auto track = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Piano", "builtin.piano"));
    tests.expect(editor.project().tracks.size() == 1, "track must be added");

    const auto clip = std::get<domain::EntityId>(
        editor.addMidiClip(track, "Clip", domain::HalfOpenTickRange{0, 3840}));
    tests.expect(editor.project().tracks[0].clips.size() == 1, "clip must be added");

    auto noteResult = editor.addNote(track, clip, 0, 960, 60, 100);
    tests.expect(isOk(noteResult), "in-range note must be added");

    // Note outside the clip range must be rejected.
    tests.expect(isError(editor.addNote(track, clip, 4000, 960, 60, 100)),
        "note past clip end must be rejected");
    // Invalid pitch must be rejected.
    tests.expect(isError(editor.addNote(track, clip, 0, 960, 200, 100)),
        "out-of-range pitch must be rejected");

    tests.expect(editor.project().tracks[0].clips[0].notes.size() == 1,
        "only the valid note must remain");
}

void testUndoRedo(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"u"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};

    const auto before = editor.project();
    const auto track = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Bass", "builtin.bass"));
    std::ignore = track;
    tests.expect(editor.canUndo(), "history must allow undo after a command");

    editor.undo();
    tests.expect(editor.project() == before, "undo must restore the prior project exactly");
    tests.expect(editor.canRedo(), "redo must be available after undo");

    editor.redo();
    tests.expect(editor.project().tracks.size() == 1, "redo must reapply the command");
}

void testChordIsOneUndoStep(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"c"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};

    const auto track = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Piano", "builtin.piano"));
    const auto clip = std::get<domain::EntityId>(
        editor.addMidiClip(track, "Clip", domain::HalfOpenTickRange{0, 3840}));

    domain::ChordSpec spec;
    spec.rootPitchClass = 0;
    spec.octave = 4;
    spec.quality = domain::ChordQuality::major;
    const auto withoutChord = editor.project();

    auto inserted = editor.insertChord(track, clip, 0, 960, spec);
    tests.expect(isOk(inserted), "chord insertion must succeed in range");
    tests.expect(editor.project().tracks[0].clips[0].notes.size() == 3,
        "C major chord must insert three notes");

    editor.undo();
    tests.expect(editor.project() == withoutChord,
        "a single undo must remove the entire chord");
}

}  // namespace

int runMidiSongTests() {
    tests::TestContext tests{"S1 MIDI song domain and commands"};
    testTempoMap(tests);
    testChordTheory(tests);
    testTrackAndNoteCommands(tests);
    testUndoRedo(tests);
    testChordIsOneUndoStep(tests);
    return tests.finish();
}
