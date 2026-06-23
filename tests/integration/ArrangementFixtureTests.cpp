#include "audio/contracts/AudioRenderer.h"
#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/FakeAudioDevice.h"
#include "audio/runtime/ProjectCompiler.h"
#include "audio/runtime/ProjectRenderer.h"
#include "domain/Instrument.h"
#include "persistence/MidiFile.h"
#include "persistence/ProjectFile.h"
#include "persistence/ProjectSerializer.h"
#include "tests/RealtimeAllocationProbe.h"
#include "tests/TestRunner.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>
#include <tuple>
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

[[nodiscard]] bool sameMusicalTracks(
    const domain::Project& original, const domain::Project& imported) {
    if (original.tracks.size() != imported.tracks.size()) return false;
    for (std::size_t index = 0; index < original.tracks.size(); ++index) {
        const auto& left = original.tracks[index];
        const auto& right = imported.tracks[index];
        if (left.name != right.name || left.instrumentId != right.instrumentId
            || left.clips.size() != right.clips.size()) {
            return false;
        }
        for (std::size_t clipIndex = 0; clipIndex < left.clips.size(); ++clipIndex) {
            const auto& leftNotes = left.clips[clipIndex].notes;
            const auto& rightNotes = right.clips[clipIndex].notes;
            if (leftNotes.size() != rightNotes.size()) return false;
            for (std::size_t noteIndex = 0; noteIndex < leftNotes.size(); ++noteIndex) {
                const auto& a = leftNotes[noteIndex];
                const auto& b = rightNotes[noteIndex];
                if (a.start != b.start || a.duration != b.duration || a.pitch != b.pitch
                    || a.velocity != b.velocity) {
                    return false;
                }
            }
        }
    }
    return true;
}

domain::Project singleInstrumentProject(const std::string_view instrumentId, const float pan = 0.0F) {
    domain::Project project;
    project.id = {"render-project"};
    project.name = "Render";
    domain::InstrumentTrack track;
    track.id = {"render-track"};
    track.name = "Voice";
    track.instrumentId = std::string{instrumentId};
    track.pan = pan;
    domain::MidiClip clip;
    clip.id = {"render-clip"};
    clip.name = "Note";
    clip.range = {0, domain::kPpq};
    clip.notes.push_back({{"render-note"}, 0, domain::kPpq, 60, 100});
    track.clips.push_back(std::move(clip));
    project.tracks.push_back(std::move(track));
    return project;
}

audio::FakeDeviceReport renderProject(const domain::Project& project) {
    const audio::AudioSpec spec{48000.0, 256, 2};
    const auto snapshot = audio::ProjectCompiler::compile(project, 1);
    audio::ProjectRenderer renderer{snapshot};
    return audio::FakeAudioDevice{spec}.render(renderer, snapshot.lengthSamples);
}

void testMidiRoundTrip(tests::TestContext& tests) {
    const auto project = audio::CanonicalVerse::makeProject();
    const auto first = persistence::MidiFile::exportBytes(project);
    const auto second = persistence::MidiFile::exportBytes(project);
    tests.expect(isOk(first), "canonical S2 project must export as Standard MIDI format 1");
    tests.expect(isOk(first) && isOk(second)
            && std::get<std::vector<std::uint8_t>>(first)
                == std::get<std::vector<std::uint8_t>>(second),
        "MIDI export bytes must be deterministic");
    if (!isOk(first)) return;

    const auto& bytes = std::get<std::vector<std::uint8_t>>(first);
    tests.expect(bytes.size() > 14 && bytes[0] == 'M' && bytes[1] == 'T'
            && bytes[8] == 0 && bytes[9] == 1,
        "export must contain a format-1 MThd header");
    const auto importedResult = persistence::MidiFile::importBytes(bytes);
    tests.expect(isOk(importedResult), "Composer must import its standards-compliant MIDI export");
    if (!isOk(importedResult)) return;
    const auto& imported = std::get<domain::Project>(importedResult);
    tests.expect(imported.tempoMap.beatsPerMinute() == project.tempoMap.beatsPerMinute(),
        "MIDI tempo meta event must round-trip");
    tests.expect(imported.timeSignatureNumerator == project.timeSignatureNumerator
            && imported.timeSignatureDenominator == project.timeSignatureDenominator,
        "MIDI time-signature meta event must round-trip");
    tests.expect(sameMusicalTracks(project, imported),
        "multitrack names, instrument programs, notes, velocities, and timing must round-trip");

    const auto reexported = persistence::MidiFile::exportBytes(imported);
    tests.expect(isOk(reexported)
            && std::get<std::vector<std::uint8_t>>(reexported) == bytes,
        "imported MIDI must re-export to the same canonical bytes");

    const auto originalRender = renderProject(project);
    const auto importedRender = renderProject(imported);
    tests.expect(originalRender.sampleHash == importedRender.sampleHash,
        "MIDI round-trip must preserve the deterministic audible render");

    auto mixed = project;
    mixed.tracks[0].volume = 1.25F;
    mixed.tracks[0].pan = -0.4F;
    mixed.tracks[1].muted = true;
    mixed.tracks[2].soloed = true;
    const auto manifest = persistence::ProjectSerializer::serialize(mixed);
    const auto reopened = persistence::ProjectSerializer::parse(manifest);
    tests.expect(isOk(reopened) && std::get<domain::Project>(reopened) == mixed,
        "S2 mixer and instrument state must survive project save/reopen exactly");
}

void testOverlappingMidiAndMalformedInput(tests::TestContext& tests) {
    auto project = singleInstrumentProject("builtin.piano");
    auto& notes = project.tracks.front().clips.front().notes;
    notes.front().duration = domain::kPpq;
    notes.push_back({{"overlap"}, domain::kPpq / 2, domain::kPpq, 60, 90});
    project.tracks.front().clips.front().range = {0, domain::kPpq * 2};
    const auto exported = persistence::MidiFile::exportBytes(project);
    tests.expect(isOk(exported), "overlapping same-pitch notes must export");
    if (!isOk(exported)) return;
    const auto bytes = std::get<std::vector<std::uint8_t>>(exported);
    const auto imported = persistence::MidiFile::importBytes(bytes);
    tests.expect(isOk(imported), "overlapping same-pitch note-ons must import");
    if (isOk(imported)) {
        const auto& importedNotes =
            std::get<domain::Project>(imported).tracks.front().clips.front().notes;
        tests.expect(importedNotes.size() == 2 && importedNotes[0].duration == domain::kPpq
                && importedNotes[1].duration == domain::kPpq,
            "overlapping same-pitch notes must retain both note lifetimes");
    }

    tests.expect(isError(persistence::MidiFile::importBytes({})),
        "empty MIDI input must return a typed error");
    auto badFormat = bytes;
    badFormat[9] = 2;
    tests.expect(isError(persistence::MidiFile::importBytes(badFormat)),
        "unsupported MIDI format 2 must be rejected");
    auto smpte = bytes;
    smpte[12] = 0x80;
    tests.expect(isError(persistence::MidiFile::importBytes(smpte)),
        "SMPTE time division must be rejected by the musical-tick importer");
    auto truncated = bytes;
    truncated.pop_back();
    tests.expect(isError(persistence::MidiFile::importBytes(truncated)),
        "truncated MIDI chunks must be rejected without over-read");

    auto invalidProject = project;
    invalidProject.timeSignatureDenominator = 3;
    tests.expect(isError(persistence::MidiFile::exportBytes(invalidProject)),
        "non-power-of-two time signatures must fail with a typed export error");
    invalidProject = project;
    invalidProject.tracks.front().clips.front().notes.front().start = -1;
    tests.expect(isError(persistence::MidiFile::exportBytes(invalidProject)),
        "invalid project notes must not be silently clamped during export");
}

void testInstrumentMixerAndGoldenRender(tests::TestContext& tests) {
    std::set<std::uint64_t> hashes;
    for (const auto& instrument : domain::kBuiltinInstruments) {
        const auto report = renderProject(singleInstrumentProject(instrument.instrumentId));
        tests.expect(report.peakMagnitude > 0.0 && report.peakMagnitude <= 1.0,
            "every built-in instrument must render a bounded non-silent voice");
        hashes.insert(report.sampleHash);
    }
    tests.expect(hashes.size() == domain::kBuiltinInstruments.size(),
        "all five built-in timbres must produce distinct deterministic renders");

    auto leftProject = singleInstrumentProject("builtin.synth", -1.0F);
    auto snapshot = audio::ProjectCompiler::compile(leftProject, 1);
    audio::ProjectRenderer renderer{snapshot};
    renderer.prepare({48000.0, 256, 2});
    std::array<std::vector<float>, 2> samples{
        std::vector<float>(256), std::vector<float>(256)};
    std::array<float*, 2> channels{samples[0].data(), samples[1].data()};
    renderer.render({channels.data(), 2, 256, 0});
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (std::size_t index = 0; index < samples[0].size(); ++index) {
        leftEnergy += std::abs(samples[0][index]);
        rightEnergy += std::abs(samples[1][index]);
    }
    tests.expect(leftEnergy > 0.0 && rightEnergy < 0.0001,
        "hard-left pan must route the voice to the left channel only");

    auto soloProject = singleInstrumentProject("builtin.piano");
    auto second = soloProject.tracks.front();
    second.id = {"second"};
    second.name = "Second";
    second.clips.front().id = {"second-clip"};
    second.clips.front().notes.front().id = {"second-note"};
    soloProject.tracks.front().soloed = true;
    soloProject.tracks.push_back(second);
    const auto soloSnapshot = audio::ProjectCompiler::compile(soloProject, 2);
    tests.expect(soloSnapshot.notes.size() == 2 && soloSnapshot.notes[0].amplitude > 0.0F
            && soloSnapshot.notes[1].amplitude == 0.0F,
        "when any track is soloed, non-solo tracks must be inaudible");
    soloProject.tracks.front().muted = true;
    const auto mutedSolo = audio::ProjectCompiler::compile(soloProject, 3);
    tests.expect(mutedSolo.notes.front().amplitude == 0.0F,
        "mute must win over solo on the same track");

    const auto canonical = audio::CanonicalVerse::makeProject();
    const auto first = renderProject(canonical);
    const auto secondRender = renderProject(canonical);
    tests.expect(first.sampleHash == secondRender.sampleHash,
        "S2 canonical golden render must repeat bit-for-bit");
    tests.expect(first.p99Milliseconds < 3.2,
        "S2 render callback p99 must stay under the 48k/256 deadline budget");

    tests::resetRealtimeAllocationCount();
    std::ignore = renderProject(canonical);
    tests.expect(tests::realtimeAllocationCount() == 0,
        "S2 built-in instrument callback must allocate zero bytes");
}

void testProjectFileRoundTrip(tests::TestContext& tests) {
    const auto path = std::filesystem::current_path() / "composer-project-roundtrip-test.json";
    auto backup = path;
    backup += ".bak";
    auto temporary = path;
    temporary += ".tmp";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(backup, ignored);
    std::filesystem::remove(temporary, ignored);

    const auto original = audio::CanonicalVerse::makeProject();
    const auto firstSave = persistence::ProjectFile::save(path, original);
    const auto reopened = persistence::ProjectFile::load(path);
    tests.expect(isOk(firstSave) && isOk(reopened)
            && std::get<domain::Project>(reopened) == original,
        "project file save/reopen must preserve the complete S2 model");

    auto updated = original;
    updated.name = "Updated arrangement";
    updated.tracks.front().pan = -0.5F;
    const auto secondSave = persistence::ProjectFile::save(path, updated);
    const auto active = persistence::ProjectFile::load(path);
    const auto previous = persistence::ProjectFile::load(backup);
    tests.expect(isOk(secondSave) && isOk(active)
            && std::get<domain::Project>(active) == updated,
        "second save must atomically replace the active project");
    tests.expect(isOk(previous) && std::get<domain::Project>(previous) == original,
        "second save must retain the prior verified project as .bak");

    {
        std::ofstream corrupt{path, std::ios::binary | std::ios::trunc};
        corrupt << "{not-json";
    }
    tests.expect(isError(persistence::ProjectFile::load(path)),
        "malformed project files must return a typed error instead of partial state");
    tests.expect(isError(persistence::ProjectFile::save({}, original)),
        "empty project save paths must be rejected");

    std::filesystem::remove(path, ignored);
    std::filesystem::remove(backup, ignored);
    std::filesystem::remove(temporary, ignored);
}

}  // namespace

int runArrangementFixtureTests() {
    composer::tests::TestContext tests{"S2 arrangement and interoperability gates"};
    testMidiRoundTrip(tests);
    testOverlappingMidiAndMalformedInput(tests);
    testInstrumentMixerAndGoldenRender(tests);
    testProjectFileRoundTrip(tests);
    return tests.finish();
}
