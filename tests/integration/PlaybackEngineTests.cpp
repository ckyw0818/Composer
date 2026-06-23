#include "audio/contracts/AudioRenderer.h"
#include "audio/contracts/RealtimeSafety.h"
#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/PlaybackEngine.h"
#include "domain/Types.h"
#include "tests/RealtimeAllocationProbe.h"
#include "tests/TestRunner.h"

#include <array>
#include <algorithm>
#include <vector>

// Verifies the S1 playback engine: transport state, looping playhead, and a zero-allocation audio
// callback. The realtime allocation probe asserts process() never touches the heap.
int runPlaybackEngineTests() {
    using namespace composer;
    tests::TestContext tests{"S1 playback engine"};

    audio::PlaybackEngine engine;
    const audio::AudioSpec spec{
        .sampleRate = audio::CanonicalVerse::sampleRate,
        .maximumBlockSize = 256,
        .outputChannels = 2};
    engine.prepare(spec);
    engine.setProject(audio::CanonicalVerse::makeProject(), 1);

    tests.expect(engine.lengthSamples() > 0, "engine must report a non-zero project length");
    tests.expect(!engine.isPlaying(), "engine must start stopped");

    // Output buffers for one block.
    std::array<std::vector<float>, 2> channelData{
        std::vector<float>(spec.maximumBlockSize, 1.0F),
        std::vector<float>(spec.maximumBlockSize, 1.0F)};
    std::array<float*, 2> outputs{channelData[0].data(), channelData[1].data()};
    const audio::RenderBlock block{
        .outputs = outputs.data(),
        .outputChannels = 2,
        .frameCount = spec.maximumBlockSize,
        .startSample = 0};

    // Stopped transport renders silence and does not advance the playhead.
    engine.process(block);
    bool silent = true;
    for (const float sample : channelData[0]) {
        if (sample != 0.0F) {
            silent = false;
            break;
        }
    }
    tests.expect(silent, "stopped transport must render silence");
    tests.expect(engine.playheadSamples() == 0, "stopped transport must not advance the playhead");

    // Playing advances the playhead by the block size.
    engine.play();
    engine.process(block);
    tests.expect(engine.isPlaying(), "engine must report playing after play()");
    tests.expect(engine.playheadSamples() == static_cast<domain::ProjectSample>(spec.maximumBlockSize),
        "playing transport must advance the playhead by one block");

    // Zero-allocation guard on the audio callback.
    tests::resetRealtimeAllocationCount();
    {
        audio::RealtimeSafety::Scope scope;
        engine.process(block);
    }
    tests.expect(tests::realtimeAllocationCount() == 0,
        "playback process() must perform zero allocations");

    // Looping: seek near the end. The block that crosses the end advances past length; the next
    // block detects start >= length and wraps to the beginning rather than stopping.
    engine.setLooping(true);
    engine.setPlayheadSamples(engine.lengthSamples() - 8);
    engine.process(block);  // crosses the end
    tests.expect(engine.isPlaying(), "looping transport must keep playing past the end");
    engine.process(block);  // wraps
    tests.expect(engine.isPlaying(), "looping transport must still be playing after wrap");
    tests.expect(engine.playheadSamples() < engine.lengthSamples(),
        "looping transport must wrap the playhead back into the project");

    // Non-looping: at the end it stops.
    engine.setLooping(false);
    engine.setPlayheadSamples(engine.lengthSamples());
    engine.process(block);
    tests.expect(!engine.isPlaying(), "non-looping transport must stop at the end");

    // Metronome follows the project tempo and remains audible even when every track is muted.
    auto mutedProject = audio::CanonicalVerse::makeProject();
    for (auto& track : mutedProject.tracks) track.muted = true;
    engine.setProject(mutedProject, 2);
    engine.setMetronomeEnabled(true);
    engine.rewind();
    engine.play();
    for (auto& channel : channelData) std::fill(channel.begin(), channel.end(), 0.0F);
    tests::resetRealtimeAllocationCount();
    {
        audio::RealtimeSafety::Scope scope;
        engine.process(block);
    }
    bool clickAudible = false;
    for (const float sample : channelData[0]) {
        if (sample != 0.0F) {
            clickAudible = true;
            break;
        }
    }
    tests.expect(clickAudible, "enabled metronome must render a beat click at the playhead");
    tests.expect(tests::realtimeAllocationCount() == 0,
        "metronome mixing must perform zero audio-thread allocations");
    engine.setMetronomeEnabled(false);

    return tests.finish();
}
