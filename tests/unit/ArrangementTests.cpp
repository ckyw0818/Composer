#include "application/Result.h"
#include "commands/ProjectEditor.h"
#include "domain/IdSource.h"
#include "domain/Instrument.h"
#include "domain/Rhythm.h"
#include "tests/TestRunner.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <tuple>
#include <variant>

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

void testInstrumentCatalog(tests::TestContext& tests) {
    tests.expect(domain::kBuiltinInstruments.size() == 5,
        "S2 catalog must contain piano, guitar, bass, drums, and synth");
    std::set<std::string_view> ids;
    for (const auto& instrument : domain::kBuiltinInstruments) {
        ids.insert(instrument.instrumentId);
        tests.expect(instrument.lowestPitch >= domain::kMinPitch
                && instrument.highestPitch <= domain::kMaxPitch
                && instrument.lowestPitch <= instrument.highestPitch,
            "every built-in instrument must publish a valid playable range");
        tests.expect(domain::findInstrument(instrument.instrumentId).has_value(),
            "every catalog id must resolve through findInstrument");
    }
    tests.expect(ids.size() == domain::kBuiltinInstruments.size(),
        "built-in instrument ids must be unique");
    tests.expect(!domain::findInstrument("missing.pack").has_value(),
        "unknown instrument ids must remain offline/unresolved");
    tests.expect(domain::defaultPatternFor(domain::InstrumentRole::guitar)
            == domain::RhythmPattern::strum,
        "guitar must default to strum");
    tests.expect(domain::defaultPatternFor(domain::InstrumentRole::bass)
            == domain::RhythmPattern::bassPulse,
        "bass must default to bass pulse");
}

domain::RhythmRequest baseRhythm() {
    domain::RhythmRequest request;
    request.pattern = domain::RhythmPattern::block;
    request.start = 0;
    request.length = domain::kPpq * 2;
    request.subdivision = domain::kPpq / 2;
    request.seed = 42;
    request.voices = {{60, 96}, {64, 96}, {67, 96}};
    return request;
}

void testRhythmGenerator(tests::TestContext& tests) {
    constexpr std::array patterns{
        domain::RhythmPattern::sustained,
        domain::RhythmPattern::block,
        domain::RhythmPattern::arpeggio,
        domain::RhythmPattern::strum,
        domain::RhythmPattern::bassPulse};
    for (const auto pattern : patterns) {
        auto request = baseRhythm();
        request.pattern = pattern;
        const auto first = domain::generateRhythm(request);
        const auto second = domain::generateRhythm(request);
        tests.expect(isOk(first), "every S2 rhythm pattern must generate notes");
        tests.expect(isOk(first) && isOk(second)
                && std::get<std::vector<domain::GeneratedNote>>(first)
                    == std::get<std::vector<domain::GeneratedNote>>(second),
            "same rhythm seed and request must be byte-deterministic");
        if (!isOk(first)) continue;
        const auto& notes = std::get<std::vector<domain::GeneratedNote>>(first);
        tests.expect(!notes.empty(), "a valid rhythm request must not produce an empty preview");
        tests.expect(std::is_sorted(notes.begin(), notes.end(),
                         [](const auto& left, const auto& right) {
                             return left.start < right.start
                                 || (left.start == right.start && left.pitch < right.pitch);
                         }),
            "generated notes must use canonical start/pitch ordering");
        for (const auto& note : notes) {
            tests.expect(note.start >= request.start && note.end() <= request.start + request.length
                    && note.duration > 0,
                "generated notes must remain positive and inside the selected span");
        }
    }

    auto seeded = baseRhythm();
    const auto seed42 = std::get<std::vector<domain::GeneratedNote>>(domain::generateRhythm(seeded));
    seeded.seed = 43;
    const auto seed43 = std::get<std::vector<domain::GeneratedNote>>(domain::generateRhythm(seeded));
    tests.expect(seed42 != seed43, "different seeds must alter humanized block velocities");

    auto duplicate = baseRhythm();
    duplicate.pattern = domain::RhythmPattern::sustained;
    duplicate.voices = {{60, 80}, {60, 110}};
    const auto deduplicated = std::get<std::vector<domain::GeneratedNote>>(
        domain::generateRhythm(duplicate));
    tests.expect(deduplicated.size() == 1 && deduplicated.front().velocity == 110,
        "overlapping source pitches must become one voice at the loudest velocity");

    auto tiny = baseRhythm();
    tiny.subdivision = 1;
    const auto tinyResult = domain::generateRhythm(tiny);
    tests.expect(isOk(tinyResult) && !std::get<std::vector<domain::GeneratedNote>>(tinyResult).empty(),
        "one-tick subdivisions must still create positive notes");

    auto invalid = baseRhythm();
    invalid.voices.clear();
    tests.expect(isError(domain::generateRhythm(invalid)), "rhythm without voices must fail");
    invalid = baseRhythm();
    invalid.length = 0;
    tests.expect(isError(domain::generateRhythm(invalid)), "zero-length rhythm must fail");
    invalid = baseRhythm();
    invalid.start = -1;
    tests.expect(isError(domain::generateRhythm(invalid)), "negative rhythm start must fail");
    invalid = baseRhythm();
    invalid.subdivision = 0;
    tests.expect(isError(domain::generateRhythm(invalid)), "zero subdivision must fail");
    invalid = baseRhythm();
    invalid.voices.front().pitch = 128;
    tests.expect(isError(domain::generateRhythm(invalid)), "invalid source pitch must fail");
    invalid = baseRhythm();
    invalid.voices.front().velocity = 0;
    tests.expect(isError(domain::generateRhythm(invalid)), "invalid source velocity must fail");
    invalid = baseRhythm();
    invalid.start = std::numeric_limits<domain::Tick>::max() - 1;
    invalid.length = 4;
    tests.expect(isError(domain::generateRhythm(invalid)), "overflowing rhythm span must fail");
}

void testRhythmCommandAndMixerUndo(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"arr"};
    domain::Project seed;
    seed.id = ids.next();
    commands::ProjectEditor editor{seed, ids};
    const auto track = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Guitar", "builtin.guitar"));
    const auto clip = std::get<domain::EntityId>(editor.addMidiClip(
        track, "Verse", {0, domain::kPpq * 4}));
    domain::ChordSpec chord;
    chord.rootPitchClass = 0;
    chord.octave = 4;
    const auto sources = std::get<std::vector<domain::EntityId>>(
        editor.insertChord(track, clip, 0, domain::kPpq * 4, chord));

    commands::ProjectEditor::RhythmApply request;
    request.pattern = domain::RhythmPattern::strum;
    request.subdivision = domain::kPpq;
    request.seed = 7;
    request.expectedRevision = editor.revision();
    request.sourceNoteIds = sources;
    const auto beforePreview = editor.project();
    const auto revisionBeforePreview = editor.revision();
    const auto preview = editor.previewRhythm(track, clip, request);
    tests.expect(isOk(preview), "valid chord selection must preview a strum");
    tests.expect(editor.project() == beforePreview && editor.revision() == revisionBeforePreview,
        "preview must not mutate project state or undo history");

    const auto beforeApply = editor.project();
    const auto applied = editor.applyRhythm(track, clip, request);
    tests.expect(isOk(applied), "previewed rhythm must apply");
    tests.expect(editor.project().tracks.front().clips.front().notes.size()
            == std::get<std::vector<domain::GeneratedNote>>(preview).size(),
        "apply must replace source notes with the previewed ordinary notes");
    editor.undo();
    tests.expect(editor.project() == beforeApply,
        "one undo must restore the entire pre-rhythm chord selection");

    request.expectedRevision = editor.revision();
    std::ignore = editor.setTrackMuted(track, true);
    const auto stale = editor.applyRhythm(track, clip, request);
    tests.expect(isError(stale)
            && std::get<application::Error>(stale).code == application::ErrorCode::staleRevision,
        "apply must reject a preview after the project revision changes");

    const auto revision = editor.revision();
    tests.expect(isOk(editor.setTrackMuted(track, true)) && editor.revision() == revision,
        "setting an unchanged mixer value must not create a no-op undo entry");
    tests.expect(isOk(editor.setTrackVolume(track, 1.5F)), "valid track volume must apply");
    tests.expect(isOk(editor.setTrackPan(track, -0.75F)), "valid track pan must apply");
    tests.expect(isOk(editor.setTrackSoloed(track, true)), "solo state must apply");
    tests.expect(isError(editor.setTrackVolume(track, -0.1F)), "negative volume must fail");
    tests.expect(isError(editor.setTrackVolume(
                     track, std::numeric_limits<float>::quiet_NaN())),
        "NaN volume must fail");
    tests.expect(isError(editor.setTrackPan(track, 1.1F)), "out-of-range pan must fail");
    tests.expect(isError(editor.setTrackMuted({"missing"}, true)), "missing mixer track must fail");
}

}  // namespace

int runArrangementTests() {
    composer::tests::TestContext tests{"S2 arrangement domain and commands"};
    testInstrumentCatalog(tests);
    testRhythmGenerator(tests);
    testRhythmCommandAndMixerUndo(tests);
    return tests.finish();
}
