#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/ClickRenderer.h"
#include "audio/runtime/FakeAudioDevice.h"
#include "tests/RealtimeAllocationProbe.h"
#include "tests/TestRunner.h"

#include <cstdint>
#include <iostream>

int runAudioFoundationTests() {
    constexpr std::uint64_t expectedGoldenHash = 0x74e1f767ebf924a5ULL;
    composer::tests::TestContext tests{"S0 fake device release gate"};
    const composer::audio::AudioSpec spec{
        .sampleRate = 48000.0,
        .maximumBlockSize = 256,
        .outputChannels = 2};
    const auto snapshot = composer::audio::CanonicalVerse::makeSnapshot();

    composer::audio::ClickRenderer firstRenderer{snapshot};
    composer::audio::ClickRenderer secondRenderer{snapshot};
    composer::audio::FakeAudioDevice device{spec};
    const auto first = device.render(firstRenderer, snapshot.lengthSamples);
    const auto second = device.render(secondRenderer, snapshot.lengthSamples);
    tests.expect(first.sampleHash == second.sampleHash, "fixture render must be deterministic");
    if (first.sampleHash != expectedGoldenHash) {
        std::cerr << "Observed golden hash: 0x" << std::hex << first.sampleHash << std::dec << '\n';
    }
    tests.expect(first.sampleHash == expectedGoldenHash, "fixture golden hash changed");
    tests.expect(first.callbackCount == 3000, "8-bar fixture must render in 3,000 callbacks");
    tests.expect(first.peakMagnitude > 0.0 && first.peakMagnitude <= 1.0,
        "fixture must produce bounded non-silent output");

    composer::tests::resetRealtimeAllocationCount();
    composer::audio::ClickRenderer guardedRenderer{snapshot};
    const auto guarded = device.render(guardedRenderer, snapshot.lengthSamples);
    tests.expect(composer::tests::realtimeAllocationCount() == 0,
        "audio callback must perform zero allocations");
    tests.expect(guarded.p99Milliseconds < 3.2, "audio callback p99 must stay below 3.2 ms");
    return tests.finish();
}
