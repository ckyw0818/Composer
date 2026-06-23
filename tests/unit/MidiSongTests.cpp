#include "commands/ProjectEditor.h"
#include "domain/Chord.h"
#include "domain/IdSource.h"
#include "domain/Project.h"
#include "domain/TempoMap.h"
#include "tests/TestRunner.h"
#include "ui/TimelineViewport.h"

#include <algorithm>
#include <cmath>
#include <variant>
#include <tuple>
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

    const auto beforeExtension = editor.project();
    tests.expect(isOk(editor.addNote(track, clip, 4000, 960, 60, 100))
            && editor.project().tracks[0].clips[0].range.end == 4960,
        "adding past the initial clip end must extend the elastic timeline");
    editor.undo();
    tests.expect(editor.project() == beforeExtension,
        "one undo must remove the note and restore the previous clip end");
    // Invalid pitch must be rejected.
    tests.expect(isError(editor.addNote(track, clip, 0, 960, 200, 100)),
        "out-of-range pitch must be rejected");

    tests.expect(editor.project().tracks[0].clips[0].notes.size() == 1,
        "only committed valid notes must remain");
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

void testCompleteTrackWorkflow(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"workflow"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};
    const auto beforeCompoundAdd = editor.project();
    const auto compoundTrack = editor.addInstrumentTrackWithMidiClip(
        "Synth", "builtin.synth", "Verse", {0, domain::kPpq * 8});
    tests.expect(isOk(compoundTrack) && editor.project().tracks.size() == 1
            && editor.project().tracks.front().clips.size() == 1,
        "UI add-instrument command must create its initial MIDI clip atomically");
    editor.undo();
    tests.expect(editor.project() == beforeCompoundAdd,
        "one undo must remove the newly added instrument and initial clip");
    tests.expect(isError(editor.addInstrumentTrackWithMidiClip(
                     "Bad", "builtin.piano", "Clip", {10, 10})),
        "compound add must reject a zero-length initial clip");

    const auto piano = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Piano", "builtin.piano"));
    const auto clip = std::get<domain::EntityId>(
        editor.addMidiClip(piano, "Verse", {0, domain::kPpq * 4}));
    std::ignore = std::get<domain::EntityId>(editor.addNote(
        piano, clip, 0, domain::kPpq, 60, 100));

    const auto duplicated = std::get<domain::EntityId>(editor.duplicateTrack(piano));
    tests.expect(editor.project().tracks.size() == 2,
        "duplicate must insert a second track");
    const auto& original = editor.project().tracks[0];
    const auto& copy = editor.project().tracks[1];
    tests.expect(copy.id == duplicated && copy.id != original.id && copy.name == "Piano Copy",
        "duplicated track must have a new id and copy name");
    tests.expect(copy.clips.size() == 1 && copy.clips[0].id != original.clips[0].id
            && copy.clips[0].notes[0].id != original.clips[0].notes[0].id,
        "duplicate must issue new ids for every nested clip and note");

    const auto revisionBeforeNoOp = editor.revision();
    tests.expect(isOk(editor.renameTrack(duplicated, "Piano Copy"))
            && isOk(editor.reorderTrack(duplicated, 1))
            && editor.revision() == revisionBeforeNoOp,
        "no-op rename and reorder must not add undo history");
    tests.expect(isOk(editor.renameTrack(duplicated, "Lead")),
        "track rename must succeed");
    tests.expect(isOk(editor.reorderTrack(duplicated, 0))
            && editor.project().tracks.front().id == duplicated,
        "track reorder must move stable identity");

    const auto beforeDelete = editor.project();
    tests.expect(isOk(editor.removeTrack(duplicated)) && editor.project().tracks.size() == 1,
        "track delete must remove the selected instrument");
    editor.undo();
    tests.expect(editor.project() == beforeDelete,
        "one undo must restore a deleted track with all notes and mixer state");
    tests.expect(isError(editor.duplicateTrack({"missing"}))
            && isError(editor.removeTrack({"missing"})),
        "missing track management commands must return typed errors");
}

void testTempoAndChordPreview(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"preview"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};
    const auto piano = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Piano", "builtin.piano"));
    const auto pianoClip = std::get<domain::EntityId>(
        editor.addMidiClip(piano, "Verse", {0, domain::kPpq * 4}));
    const auto drums = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Drums", "builtin.drums"));
    const auto drumClip = std::get<domain::EntityId>(
        editor.addMidiClip(drums, "Beat", {0, domain::kPpq * 4}));

    domain::ChordSpec spec;
    const auto beforePreview = editor.project();
    const auto revision = editor.revision();
    const auto preview = editor.previewChord(piano, pianoClip, 0, domain::kPpq / 2, spec);
    tests.expect(isOk(preview)
            && std::get<std::vector<domain::GeneratedNote>>(preview).size() == 3,
        "pitched track chord preview must return ordinary ghost notes");
    tests.expect(editor.project() == beforePreview && editor.revision() == revision,
        "chord preview must not mutate the project or undo history");
    tests.expect(isError(editor.previewChord(drums, drumClip, 0, domain::kPpq, spec)),
        "drum tracks must reject the chord tool with a typed reason");
    const auto elasticPreview = editor.previewChord(
        piano, pianoClip, domain::kPpq * 4, domain::kPpq, spec);
    tests.expect(isOk(elasticPreview) && editor.project() == beforePreview,
        "chord preview beyond the current end must remain non-mutating and available");
    const auto beforeElasticChord = editor.project();
    tests.expect(isOk(editor.insertChord(
                     piano, pianoClip, domain::kPpq * 4, domain::kPpq, spec))
            && editor.project().tracks[0].clips[0].range.end == domain::kPpq * 5,
        "inserting a chord beyond the current end must extend the clip atomically");
    editor.undo();
    tests.expect(editor.project() == beforeElasticChord,
        "undoing an elastic chord insertion must restore the old clip end");

    tests.expect(isOk(editor.setTempoBpm(90.0))
            && editor.project().tempoMap.beatsPerMinute() == 90.0,
        "tempo command must update the project tempo map");
    const auto tempoRevision = editor.revision();
    tests.expect(isOk(editor.setTempoBpm(90.0)) && editor.revision() == tempoRevision,
        "unchanged tempo must not create a no-op undo entry");
    tests.expect(isError(editor.setTempoBpm(0.0)) && isError(editor.setTempoBpm(301.0)),
        "tempo outside the supported transport range must fail");
}

void testBulkNoteEditing(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"bulk"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};
    const auto track = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Piano", "builtin.piano"));
    const auto clip = std::get<domain::EntityId>(editor.addMidiClip(
        track, "Phrase", {0, domain::kPpq * 8}));
    const auto first = std::get<domain::EntityId>(
        editor.addNote(track, clip, 0, domain::kPpq, 60, 90));
    const auto second = std::get<domain::EntityId>(
        editor.addNote(track, clip, domain::kPpq, domain::kPpq / 2, 64, 100));
    std::ignore = editor.addNote(
        track, clip, domain::kPpq * 3, domain::kPpq, 67, 110);

    const auto beforeMove = editor.project();
    const auto moveRevision = editor.revision();
    tests.expect(isOk(editor.moveNotes(
                     track, clip, {first, second}, domain::kPpq / 2, 2))
            && editor.revision() == moveRevision + 1,
        "moving a note selection must commit exactly once");
    const auto& movedNotes = editor.project().tracks[0].clips[0].notes;
    const auto movedFirst = std::find_if(movedNotes.begin(), movedNotes.end(),
        [&](const domain::NoteEvent& note) { return note.id == first; });
    const auto movedSecond = std::find_if(movedNotes.begin(), movedNotes.end(),
        [&](const domain::NoteEvent& note) { return note.id == second; });
    tests.expect(movedFirst != movedNotes.end() && movedSecond != movedNotes.end()
            && movedFirst->start == domain::kPpq / 2 && movedFirst->pitch == 62
            && movedSecond->start == domain::kPpq + domain::kPpq / 2
            && movedSecond->pitch == 66,
        "group move must preserve relative timing while applying pitch and time deltas");
    editor.undo();
    tests.expect(editor.project() == beforeMove,
        "one undo must restore every note in a group move");

    const auto beforeRejectedMove = editor.project();
    const auto rejectedRevision = editor.revision();
    tests.expect(isError(editor.moveNotes(
                     track, clip, {first, {"missing"}}, domain::kPpq, 0))
            && isError(editor.moveNotes(track, clip, {first, second}, -1, 0))
            && editor.project() == beforeRejectedMove
            && editor.revision() == rejectedRevision,
        "invalid group moves must be all-or-nothing and add no history");

    const std::vector<domain::GeneratedNote> clipboard{
        {0, domain::kPpq / 2, 60, 90},
        {domain::kPpq / 2, domain::kPpq, 64, 100},
    };
    const auto beforePaste = editor.project();
    const auto pasteRevision = editor.revision();
    const auto pastedResult = editor.pasteNotes(
        track, clip, domain::kPpq * 4, clipboard);
    tests.expect(isOk(pastedResult) && editor.revision() == pasteRevision + 1,
        "pasting a clipboard must commit all notes exactly once");
    const auto pastedIds = std::get<std::vector<domain::EntityId>>(pastedResult);
    tests.expect(pastedIds.size() == 2 && pastedIds[0] != first && pastedIds[1] != second,
        "pasted notes must receive fresh stable ids");
    editor.undo();
    tests.expect(editor.project() == beforePaste,
        "one undo must remove the complete pasted selection");

    const auto pastedAgain = std::get<std::vector<domain::EntityId>>(
        editor.pasteNotes(track, clip, domain::kPpq * 4, clipboard));
    const auto beforeDelete = editor.project();
    tests.expect(isOk(editor.deleteNotes(track, clip, pastedAgain)),
        "deleting a selected note group must succeed");
    editor.undo();
    tests.expect(editor.project() == beforeDelete,
        "one undo must restore all notes deleted as a group");

    const auto beforeExtendedPaste = editor.project();
    const auto oldEnd = beforeExtendedPaste.tracks[0].clips[0].range.end;
    tests.expect(isOk(editor.pasteNotes(track, clip, oldEnd + domain::kPpq, clipboard))
            && editor.project().tracks[0].clips[0].range.end
                == oldEnd + domain::kPpq * 2 + domain::kPpq / 2,
        "pasting beyond the current end must extend the clip to the pasted selection");
    editor.undo();
    tests.expect(editor.project() == beforeExtendedPaste,
        "undoing an elastic paste must restore the previous clip range exactly");

    const auto beforeExtendedMove = editor.project();
    const auto moveStart = oldEnd + domain::kPpq;
    tests.expect(isOk(editor.moveNotes(track, clip, {first}, moveStart, 0))
            && editor.project().tracks[0].clips[0].range.end
                == moveStart + domain::kPpq,
        "moving a selection beyond the current end must extend the clip");
    editor.undo();
    tests.expect(editor.project() == beforeExtendedMove,
        "undoing an elastic move must restore note positions and clip range");

    const auto beforeExtendedResize = editor.project();
    const auto extendedDuration = oldEnd + domain::kPpq * 2;
    tests.expect(isOk(editor.resizeNote(track, clip, second, extendedDuration))
            && editor.project().tracks[0].clips[0].range.end
                == domain::kPpq + extendedDuration,
        "resizing a note beyond the current end must extend the clip");
    editor.undo();
    tests.expect(editor.project() == beforeExtendedResize,
        "undoing an elastic resize must restore duration and clip range");
}

void testTimelineViewportCoordinates(tests::TestContext& tests) {
    ui::TimelineViewport viewport;
    viewport.setViewportWidth(480.0);
    viewport.setRange({0, domain::kPpq * 32}, true);

    const auto clickedTick = viewport.tickForX(13.0);
    tests.expect(clickedTick == 260
            && viewport.snapForInsertion(clickedTick, domain::kPpq / 2, false) == 0
            && viewport.snapNearest(clickedTick, domain::kPpq / 2) == domain::kPpq / 2,
        "note insertion must choose the clicked grid cell, not the nearest grid line");
    tests.expect(viewport.xForTick(
                     viewport.snapForInsertion(clickedTick, domain::kPpq / 2, false)) == 0,
        "an inserted note must render on the left boundary of the clicked cell");

    viewport.setRange({domain::kPpq * 4, domain::kPpq * 12}, true);
    tests.expect(viewport.tickForX(0) == domain::kPpq * 4
            && viewport.xForTick(domain::kPpq * 4) == 0,
        "the viewport origin must be the clip start rather than absolute tick zero");

    viewport.setRange({0, domain::kPpq * 32}, true);
    tests.expect(viewport.scrollByPixels(48.0)
            && viewport.tickForX(0) == domain::kPpq,
        "scrolling by one beat width must advance the view by one beat");

    viewport.setRange({0, domain::kPpq * 32}, true);
    for (int step = 0; step < 40; ++step) {
        std::ignore = viewport.scrollByPixels(480.0);
    }
    tests.expect(viewport.viewStartTick() > static_cast<double>(domain::kPpq * 32)
            && viewport.range().end > domain::kPpq * 32,
        "repeated horizontal scrolling must grow beyond the stored clip end");

    viewport.setRange({0, domain::kPpq * 32}, true);
    const int anchorX = 240;
    const auto tickUnderCursor = viewport.tickForX(anchorX);
    tests.expect(viewport.zoomAt(anchorX, 2.0)
            && viewport.pixelsPerBeat() == 96.0
            && std::abs(viewport.tickForX(anchorX) - tickUnderCursor) <= 1,
        "zoom must preserve the musical tick under the mouse cursor");
    tests.expect(!viewport.zoomAt(anchorX, 0.0),
        "invalid zoom factors must not mutate the viewport");
}

}  // namespace

int runMidiSongTests() {
    tests::TestContext tests{"S1 MIDI song domain and commands"};
    testTempoMap(tests);
    testChordTheory(tests);
    testTrackAndNoteCommands(tests);
    testUndoRedo(tests);
    testChordIsOneUndoStep(tests);
    testCompleteTrackWorkflow(tests);
    testTempoAndChordPreview(tests);
    testBulkNoteEditing(tests);
    testTimelineViewportCoordinates(tests);
    return tests.finish();
}
