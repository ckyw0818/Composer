#include "audio/contracts/RealtimeSafety.h"
#include "tests/RealtimeAllocationProbe.h"

#include <atomic>
#include <cstdlib>
#include <new>

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

int main() {
    const int foundation = runAudioFoundationTests();
    const int midiSong = runMidiSongFixtureTests();
    const int playback = runPlaybackEngineTests();
    return (foundation == 0 && midiSong == 0 && playback == 0) ? 0 : 1;
}
