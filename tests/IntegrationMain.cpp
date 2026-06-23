#include "audio/contracts/RealtimeSafety.h"
#include "tests/RealtimeAllocationProbe.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <string_view>

namespace {
std::atomic<std::size_t> realtimeAllocations{0};

void recordRealtimeAllocation() noexcept {
    if (composer::audio::RealtimeSafety::isRealtimeThread()) {
        realtimeAllocations.fetch_add(1, std::memory_order_relaxed);
    }
}
}  // namespace

void* operator new(const std::size_t size) {
    recordRealtimeAllocation();
    if (auto* memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void* operator new[](const std::size_t size) {
    recordRealtimeAllocation();
    if (auto* memory = std::malloc(size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace composer::tests {

void resetRealtimeAllocationCount() noexcept {
    realtimeAllocations.store(0, std::memory_order_relaxed);
}

std::size_t realtimeAllocationCount() noexcept {
    return realtimeAllocations.load(std::memory_order_relaxed);
}

}  // namespace composer::tests

int runAudioFoundationTests();
int runMidiSongFixtureTests();
int runPlaybackEngineTests();
int runArrangementFixtureTests();
int runRecordingFixtureTests();

int main(const int argc, const char* const* argv) {
    if (argc == 2) {
        const std::string_view suite{argv[1]};
        if (suite == "foundation") return runAudioFoundationTests();
        if (suite == "midi-song") return runMidiSongFixtureTests();
        if (suite == "playback") return runPlaybackEngineTests();
        if (suite == "arrangement") return runArrangementFixtureTests();
        if (suite == "recording") return runRecordingFixtureTests();
        return 2;
    }
    const int foundation = runAudioFoundationTests();
    const int midiSong = runMidiSongFixtureTests();
    const int playback = runPlaybackEngineTests();
    const int arrangement = runArrangementFixtureTests();
    const int recording = runRecordingFixtureTests();
    return (foundation == 0 && midiSong == 0 && playback == 0 && arrangement == 0
               && recording == 0)
        ? 0
        : 1;
}
