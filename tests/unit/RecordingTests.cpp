#include "commands/ProjectEditor.h"
#include "domain/IdSource.h"
#include "persistence/ProjectSerializer.h"
#include "recording/RecordingSession.h"
#include "tests/TestRunner.h"

#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace composer;

template <typename T>
bool isOk(const application::Result<T>& result) {
    return std::holds_alternative<T>(result);
}

void testAudioTrackCommands(tests::TestContext& tests) {
    domain::SequentialIdSource ids{"record"};
    domain::Project project;
    project.id = ids.next();
    project.name = "Recording";
    commands::ProjectEditor editor{project, ids};

    const auto instrument = std::get<domain::EntityId>(
        editor.addInstrumentTrack("Piano", "builtin.piano"));
    const auto audio = std::get<domain::EntityId>(editor.addAudioTrack("Voice", "fake-1", 0));
    tests.expect(editor.project().audioTracks.size() == 1
            && editor.project().trackOrder.size() == 2,
        "adding the first audio track must establish mixed stable track order");
    tests.expect(isOk(editor.setAudioInputRoute(audio, "fake-1", 0,
                     domain::MonitoringMode::software, 240)),
        "audio input route, software monitoring, and latency must be editable");
    tests.expect(isOk(editor.setAudioTrackArmed(audio, true)), "audio track must record-arm");

    const auto secondAudio = std::get<domain::EntityId>(
        editor.addAudioTrack("Guitar", "fake-1", 0));
    const auto duplicateArm = editor.setAudioTrackArmed(secondAudio, true);
    tests.expect(std::holds_alternative<application::Error>(duplicateArm),
        "the same device channel must not arm on two tracks silently");
    tests.expect(isOk(editor.reorderTrack(secondAudio, 0))
            && editor.project().trackOrder.front() == secondAudio,
        "audio and instrument tracks must share one presentation order");
    tests.expect(isOk(editor.renameTrack(secondAudio, "DI Guitar"))
            && editor.project().audioTracks[1].name == "DI Guitar",
        "generic track rename must include audio tracks");

    const auto clipId = std::get<domain::EntityId>(editor.addRecordedAudioClip(audio,
        "Take 1", "Audio/take-1.wav", 100, 2000, 48000.0, 1));
    const auto& compensated = editor.project().audioTracks.front().clips.front();
    tests.expect(compensated.startSample == 0 && compensated.sourceOffsetFrames == 140
            && compensated.lengthFrames == 1860,
        "latency compensation must move the clip and trim any negative timeline prefix");
    tests.expect(isOk(editor.moveAudioClip(audio, clipId, 1000)),
        "recorded audio clip must move in absolute sample time");
    tests.expect(isOk(editor.trimAudioClip(audio, clipId, 100, 60)),
        "audio trim must preserve the immutable source asset");
    const auto rightId = std::get<domain::EntityId>(
        editor.splitAudioClip(audio, clipId, 500));
    tests.expect(editor.project().audioTracks.front().clips.size() == 2,
        "audio split must create two non-destructive source windows");
    tests.expect(isOk(editor.setAudioClipFades(audio, clipId, 40, 50)),
        "fade-in and fade-out must fit inside the clip");
    tests.expect(isOk(editor.setAudioClipGain(audio, rightId, 1.5F)),
        "per-clip gain must support non-destructive level edits");
    const auto beforeCrossfade = editor.project();
    tests.expect(isOk(editor.setAudioCrossfade(audio, clipId, rightId, 100)),
        "two audio clips must support a persisted crossfade");
    editor.undo();
    tests.expect(editor.project() == beforeCrossfade,
        "one Undo must restore the complete pre-crossfade audio state");
    editor.redo();
    tests.expect(editor.project().audioTracks.front().crossfades.size() == 1,
        "Redo must restore the crossfade metadata");

    const auto serialized = persistence::ProjectSerializer::serialize(editor.project());
    const auto parsed = persistence::ProjectSerializer::parse(serialized);
    tests.expect(isOk(parsed) && std::get<domain::Project>(parsed) == editor.project(),
        "S3 audio routing, edits, and track order must round-trip exactly");

    const std::string legacy =
        R"({"schemaVersion":1,"id":"legacy","name":"Old","bpm":120,"sampleRate":48000,)"
        R"("timeSignatureNumerator":4,"timeSignatureDenominator":4,"tracks":[]})";
    const auto legacyProject = persistence::ProjectSerializer::parse(legacy);
    tests.expect(isOk(legacyProject)
            && std::get<domain::Project>(legacyProject).audioTracks.empty()
            && std::get<domain::Project>(legacyProject).trackOrder.empty(),
        "v1 MIDI-only project manifests must remain readable without synthetic audio state");
    (void) instrument;
}

void testLatencyEstimator(tests::TestContext& tests) {
    std::vector<float> reference(128, 0.0F);
    for (std::size_t index = 0; index < reference.size(); ++index) {
        reference[index] = static_cast<float>(std::sin(static_cast<double>(index) * 0.23));
    }
    std::vector<float> captured(512, 0.0F);
    constexpr std::size_t delay = 173;
    for (std::size_t index = 0; index < reference.size(); ++index) {
        captured[delay + index] = reference[index];
    }
    const auto measured = recording::LatencyEstimator::estimateRoundTripSamples(
        reference, captured, 300);
    tests.expect(isOk(measured) && std::get<std::int64_t>(measured) == delay,
        "round-trip latency measurement must recover the deterministic sample delay");
    std::fill(captured.begin(), captured.end(), 0.0F);
    tests.expect(std::holds_alternative<application::Error>(
                     recording::LatencyEstimator::estimateRoundTripSamples(
                         reference, captured, 300)),
        "latency measurement must reject a capture without the calibration signal");
}

}  // namespace

int runRecordingTests() {
    composer::tests::TestContext tests{"S3 recording domain and persistence"};
    testAudioTrackCommands(tests);
    testLatencyEstimator(tests);
    return tests.finish();
}
