#include "commands/ProjectEditor.h"
#include "domain/IdSource.h"
#include "recording/FakeRecordingDevice.h"
#include "recording/WavFile.h"
#include "tests/RealtimeAllocationProbe.h"
#include "tests/TestRunner.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <variant>

namespace {

using namespace composer;

template <typename T>
bool isOk(const application::Result<T>& result) {
    return std::holds_alternative<T>(result);
}

class TempProjectRoot final {
public:
    explicit TempProjectRoot(const std::string& name) {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("composer-" + name + "-" + std::to_string(ticks));
        std::filesystem::create_directories(path_);
    }
    ~TempProjectRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

recording::RecordingRequest requestFor(
    const TempProjectRoot& root, const domain::EntityId& trackId, const std::string& takeId) {
    recording::RecordingRequest request;
    request.projectRoot = root.path();
    request.takeId = takeId;
    request.trackId = trackId;
    request.startSample = 4800;
    request.sampleRate = 48000.0;
    request.channels = 1;
    request.firstInputChannel = 0;
    request.beatsPerMinute = 240.0;
    request.beatsPerBar = 4;
    request.countInBars = 0;
    request.fifoCapacityFrames = 65536;
    request.minimumFreeBytes = 0;
    request.expectedDurationSeconds = 1.0;
    return request;
}

domain::EntityId makeAudioTrack(commands::ProjectEditor& editor) {
    return std::get<domain::EntityId>(editor.addAudioTrack("Voice", "fake", 0));
}

void testNormalCountInRecording(tests::TestContext& tests) {
    TempProjectRoot root{"record-normal"};
    domain::SequentialIdSource ids{"normal"};
    domain::Project project;
    project.id = ids.next();
    commands::ProjectEditor editor{project, ids};
    const auto trackId = makeAudioTrack(editor);
    std::ignore = editor.setAudioInputRoute(
        trackId, "fake", 0, domain::MonitoringMode::software, 128);

    auto request = requestFor(root, trackId, "take-normal");
    request.countInBars = 1;
    request.startSample = 96000;
    recording::RecordingSession session;
    tests.expect(isOk(session.start(request)), "count-in recording must pass preflight");
    tests::resetRealtimeAllocationCount();
    const recording::FakeRecordingDevice device{48000.0, 256, 2};
    const auto deviceReport = device.run(session, 12000);
    tests.expect(tests::realtimeAllocationCount() == 0,
        "recording callback must allocate zero memory while fake input runs");
    tests.expect(deviceReport.peak > 0.0F && session.inputPeak() > 0.0F,
        "fake device must drive the input meter during count-in recording");
    const auto stopped = session.stop();
    tests.expect(isOk(stopped), "normal recording must finalize successfully");
    if (!isOk(stopped)) return;
    const auto outcome = std::get<recording::RecordingOutcome>(stopped);
    tests.expect(!outcome.failure.has_value(), "normal recording must not report a failure");
    tests.expect(outcome.take.frames == 12000,
        "recording must capture exactly the post-count-in frames");
    const auto wav = recording::WavFile::inspect(outcome.take.absolutePath);
    tests.expect(isOk(wav) && std::get<recording::WavInfo>(wav).frames == 12000,
        "finalized recording WAV must have a repaired data length");
    tests.expect(!std::filesystem::exists(root.path() / "Recovery" / "take-normal.journal"),
        "successful recording must remove its recovery journal after finalize");
    const auto clip = editor.addRecordedAudioClip(trackId, "Take Normal", outcome.take.assetPath,
        outcome.take.startSample, outcome.take.frames, outcome.take.sampleRate, outcome.take.channels);
    tests.expect(isOk(clip) && editor.project().audioTracks.front().clips.front().startSample == 95872,
        "committing a take must apply the track latency compensation to the clip start");
}

void testFaultsPreservePartialTakes(tests::TestContext& tests) {
    const recording::FakeRecordingDevice device{48000.0, 256, 2};
    struct Case final {
        const char* name;
        recording::FakeInputFault fault;
        application::ErrorCode code;
    };
    const Case cases[] = {
        {"disconnect", recording::FakeInputFault::disconnect,
            application::ErrorCode::deviceDisconnected},
        {"rate", recording::FakeInputFault::sampleRateChange,
            application::ErrorCode::sampleRateChanged},
        {"dropout", recording::FakeInputFault::dropout,
            application::ErrorCode::inputDropout},
    };

    for (const auto& scenario : cases) {
        TempProjectRoot root{std::string{"record-"} + scenario.name};
        domain::SequentialIdSource ids{scenario.name};
        domain::Project project;
        project.id = ids.next();
        commands::ProjectEditor editor{project, ids};
        const auto trackId = makeAudioTrack(editor);
        auto request = requestFor(root, trackId, std::string{"take-"} + scenario.name);
        recording::RecordingSession session;
        tests.expect(isOk(session.start(request)), "fault scenario must pass preflight");
        const auto report = device.run(session, 16000, scenario.fault, 4096);
        tests.expect(report.callbackCount > 0, "fault scenario must exercise fake callbacks");
        const auto stopped = session.stop();
        tests.expect(isOk(stopped), "recoverable device/input fault must still return a take");
        if (!isOk(stopped)) continue;
        const auto outcome = std::get<recording::RecordingOutcome>(stopped);
        tests.expect(outcome.failure.has_value() && outcome.failure->code == scenario.code,
            "fault outcome must carry the typed recording error");
        tests.expect(outcome.take.frames > 0
                && std::filesystem::exists(outcome.take.absolutePath),
            "fault outcome must preserve the captured WAV instead of discarding it");
    }
}

void testDiskFullAndCrashRecovery(tests::TestContext& tests) {
    const recording::FakeRecordingDevice device{48000.0, 256, 2};
    {
        TempProjectRoot root{"record-disk"};
        domain::SequentialIdSource ids{"disk"};
        domain::Project project;
        project.id = ids.next();
        commands::ProjectEditor editor{project, ids};
        const auto trackId = makeAudioTrack(editor);
        auto request = requestFor(root, trackId, "take-disk");
        request.failAfterDataBytes = 2048;
        recording::RecordingSession session;
        tests.expect(isOk(session.start(request)), "disk-full recording must pass preflight");
        const auto report = device.run(session, 48000);
        tests.expect(report.callbackCount > 0, "disk-full scenario must exercise fake callbacks");
        const auto stopped = session.stop();
        tests.expect(isOk(stopped), "disk-full fault must preserve a partial take");
        if (isOk(stopped)) {
            const auto outcome = std::get<recording::RecordingOutcome>(stopped);
            tests.expect(outcome.failure.has_value()
                    && outcome.failure->code == application::ErrorCode::diskSpaceLow,
                "disk-full simulation must surface DiskSpaceLowError");
            tests.expect(outcome.take.frames > 0 && outcome.take.frames < 48000,
                "disk-full partial take must be shorter than the requested capture");
        }
    }
    {
        TempProjectRoot root{"record-crash"};
        domain::SequentialIdSource ids{"crash"};
        domain::Project project;
        project.id = ids.next();
        commands::ProjectEditor editor{project, ids};
        const auto trackId = makeAudioTrack(editor);
        auto request = requestFor(root, trackId, "take-crash");
        recording::RecordingSession session;
        tests.expect(isOk(session.start(request)), "crash recovery recording must pass preflight");
        const auto report = device.run(session, 8192);
        tests.expect(report.callbackCount > 0, "crash recovery scenario must exercise fake callbacks");
        session.simulateCrash();
        tests.expect(std::filesystem::exists(root.path() / "Recovery" / "take-crash.journal"),
            "forced-exit simulation must leave the recovery journal in place");
        const auto recovered = recording::RecordingSession::recoverPending(root.path());
        tests.expect(isOk(recovered)
                && std::get<std::vector<recording::RecordedTake>>(recovered).size() == 1,
            "startup recovery must discover and repair the pending take");
        if (isOk(recovered)) {
            const auto take = std::get<std::vector<recording::RecordedTake>>(recovered).front();
            tests.expect(take.recovered && take.frames == 8192
                    && recording::WavFile::inspect(take.absolutePath).index() == 0,
                "recovered take must be a finalized playable WAV with the captured frames");
            tests.expect(isOk(editor.addRecordedAudioClip(trackId, "Recovered", take.assetPath,
                             take.startSample, take.frames, take.sampleRate, take.channels)),
                "recovered take metadata must be commit-ready for the project model");
        }
    }
}

}  // namespace

int runRecordingFixtureTests() {
    composer::tests::TestContext tests{"S3 fake-device recording and recovery"};
    testNormalCountInRecording(tests);
    testFaultsPreservePartialTakes(tests);
    testDiskFullAndCrashRecovery(tests);
    return tests.finish();
}
