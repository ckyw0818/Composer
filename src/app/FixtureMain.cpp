#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/ClickRenderer.h"
#include "audio/runtime/FakeAudioDevice.h"

#include <iomanip>
#include <iostream>
#include <string_view>

int main(const int argc, const char* const* argv) {
    if (argc != 3 || std::string_view{argv[1]} != "--fixture"
        || std::string_view{argv[2]} != "canonical-verse") {
        std::cerr << "DX-FIXTURE-005: expected --fixture canonical-verse\n";
        return 2;
    }

    auto snapshot = composer::audio::CanonicalVerse::makeSnapshot();
    composer::audio::ClickRenderer renderer{snapshot};
    composer::audio::FakeAudioDevice device{{
        .sampleRate = composer::audio::CanonicalVerse::sampleRate,
        .maximumBlockSize = 256,
        .outputChannels = 2}};
    const auto report = device.render(renderer, snapshot.lengthSamples);

    std::cout << "fixture=canonical-verse"
              << " bars=" << composer::audio::CanonicalVerse::bars
              << " tracks=" << composer::audio::CanonicalVerse::trackNames.size()
              << " callbacks=" << report.callbackCount
              << " hash=0x" << std::hex << report.sampleHash << std::dec
              << " p99_ms=" << std::fixed << std::setprecision(4) << report.p99Milliseconds
              << " peak=" << report.peakMagnitude << '\n';
    return report.peakMagnitude > 0.0 ? 0 : 1;
}
