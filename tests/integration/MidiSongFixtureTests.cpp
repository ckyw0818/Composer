#include "audio/contracts/AudioRenderer.h"
#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/FakeAudioDevice.h"
#include "audio/runtime/ProjectCompiler.h"
#include "audio/runtime/ProjectRenderer.h"
#include "persistence/ProjectSerializer.h"
#include "tests/RealtimeAllocationProbe.h"
#include "tests/TestRunner.h"

#include <iostream>
#include <variant>

// S1 exit gate: 8-bar fixture save / reload / render.
//
// 1. Build the canonical verse from the real domain command layer.
// 2. Serialize to project.json, parse it back, and require byte-equal round-trip and equal models.
// 3. Compile both the original and reloaded projects to snapshots and render them through the fake
//    device; the renders must be bit-identical and deterministic across runs, with zero audio-thread
//    allocations.
int runMidiSongFixtureTests() {
    using namespace composer;
    tests::TestContext tests{"S1 8-bar fixture save/reload/render gate"};

    const auto project = audio::CanonicalVerse::makeProject();
    tests.expect(project.tracks.size() == 4,
        "canonical verse must have piano, guitar, bass, drums");

    // --- save / reload ---
    const std::string json = persistence::ProjectSerializer::serialize(project);
    auto reloadedResult = persistence::ProjectSerializer::parse(json);
    tests.expect(std::holds_alternative<domain::Project>(reloadedResult),
        "manifest must parse back into a project");
    if (!std::holds_alternative<domain::Project>(reloadedResult)) {
        return tests.finish();
    }
    const auto& reloaded = std::get<domain::Project>(reloadedResult);
    tests.expect(reloaded == project, "reloaded project must equal the original model");

    const std::string reserialized = persistence::ProjectSerializer::serialize(reloaded);
    tests.expect(reserialized == json, "serialize(parse(serialize(p))) must be byte-stable");

    // --- render determinism ---
    const audio::AudioSpec spec{
        .sampleRate = audio::CanonicalVerse::sampleRate,
        .maximumBlockSize = 256,
        .outputChannels = 2};
    audio::FakeAudioDevice device{spec};

    const auto originalSnapshot = audio::ProjectCompiler::compile(project, 1);
    const auto reloadedSnapshot = audio::ProjectCompiler::compile(reloaded, 1);
    tests.expect(originalSnapshot.lengthSamples > 0, "compiled fixture must have non-zero length");
    tests.expect(originalSnapshot.notes.size() == reloadedSnapshot.notes.size(),
        "reloaded project must compile to the same note count");

    audio::ProjectRenderer originalRenderer{originalSnapshot};
    audio::ProjectRenderer reloadedRenderer{reloadedSnapshot};
    const auto first = device.render(originalRenderer, originalSnapshot.lengthSamples);
    const auto second = device.render(reloadedRenderer, reloadedSnapshot.lengthSamples);

    tests.expect(first.sampleHash == second.sampleHash,
        "render of original and reloaded project must be bit-identical");
    tests.expect(first.peakMagnitude > 0.0 && first.peakMagnitude <= 1.0,
        "fixture render must be bounded and non-silent");

    // Determinism across repeated renders of the same snapshot.
    audio::ProjectRenderer repeatRenderer{originalSnapshot};
    const auto third = device.render(repeatRenderer, originalSnapshot.lengthSamples);
    tests.expect(third.sampleHash == first.sampleHash, "repeated render must be deterministic");

    // 8 bars at 120 BPM/48k == 16 s == 768000 samples == 3000 blocks of 256.
    tests.expect(first.callbackCount == 3000, "8-bar verse must render in 3000 callbacks");

    // Audio-thread allocation guard on the render path.
    tests::resetRealtimeAllocationCount();
    audio::ProjectRenderer guardedRenderer{originalSnapshot};
    const auto guarded = device.render(guardedRenderer, originalSnapshot.lengthSamples);
    std::ignore = guarded;
    tests.expect(tests::realtimeAllocationCount() == 0,
        "project render callback must perform zero allocations");

    if (first.sampleHash != second.sampleHash) {
        std::cerr << "original hash: 0x" << std::hex << first.sampleHash
                  << " reloaded hash: 0x" << second.sampleHash << std::dec << '\n';
    }
    return tests.finish();
}
