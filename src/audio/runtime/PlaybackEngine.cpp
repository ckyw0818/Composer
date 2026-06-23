#include "audio/runtime/PlaybackEngine.h"

#include "audio/runtime/ProjectCompiler.h"

#include <algorithm>
#include <utility>

namespace composer::audio {

PlaybackEngine::PlaybackEngine() {
    renderer_.store(std::make_shared<ProjectRenderer>(ProjectSnapshot{}),
        std::memory_order_relaxed);
}

void PlaybackEngine::setProject(const domain::Project& project, const domain::Revision revision) {
    auto snapshot = ProjectCompiler::compile(project, revision, assetRoot_);
    length_.store(snapshot.lengthSamples, std::memory_order_relaxed);
    sampleRate_.store(snapshot.sampleRate, std::memory_order_relaxed);
    samplesPerBeat_.store(std::max<domain::ProjectSample>(
        1, project.tempoMap.ticksToSamples(domain::kPpq)), std::memory_order_relaxed);
    beatsPerBar_.store(std::max(1, project.timeSignatureNumerator),
        std::memory_order_relaxed);
    if (playhead_.load(std::memory_order_relaxed) > snapshot.lengthSamples) {
        playhead_.store(0, std::memory_order_relaxed);
    }
    auto fresh = std::make_shared<ProjectRenderer>(std::move(snapshot));
    fresh->prepare(spec_);
    renderer_.store(std::move(fresh), std::memory_order_release);
}

void PlaybackEngine::setAssetRoot(std::filesystem::path assetRoot) {
    assetRoot_ = std::move(assetRoot);
}

void PlaybackEngine::togglePlay() noexcept {
    const bool now = playing_.load(std::memory_order_relaxed);
    playing_.store(!now, std::memory_order_relaxed);
}

void PlaybackEngine::setPlayheadSamples(const domain::ProjectSample sample) noexcept {
    const auto clamped = std::max<domain::ProjectSample>(0, sample);
    playhead_.store(clamped, std::memory_order_relaxed);
}

void PlaybackEngine::prepare(const AudioSpec& spec) {
    spec_ = spec;
    sampleRate_.store(spec.sampleRate, std::memory_order_relaxed);
    if (auto current = renderer_.load(std::memory_order_acquire)) {
        current->prepare(spec);
    }
}

void PlaybackEngine::process(const RenderBlock& block) noexcept {
    // Always clear the output first so a stopped transport renders silence.
    for (std::size_t channel = 0; channel < block.outputChannels; ++channel) {
        std::fill_n(block.outputs[channel], block.frameCount, 0.0F);
    }

    if (!playing_.load(std::memory_order_relaxed)) {
        return;
    }

    auto current = renderer_.load(std::memory_order_acquire);
    if (!current) {
        return;
    }

    const auto length = length_.load(std::memory_order_relaxed);
    auto start = playhead_.load(std::memory_order_relaxed);

    if (length > 0 && start >= length) {
        if (looping_.load(std::memory_order_relaxed)) {
            start = 0;
        } else {
            playing_.store(false, std::memory_order_relaxed);
            return;
        }
    }

    const RenderBlock playBlock{
        .outputs = block.outputs,
        .outputChannels = block.outputChannels,
        .frameCount = block.frameCount,
        .startSample = start};
    current->render(playBlock);

    if (metronomeEnabled_.load(std::memory_order_relaxed)) {
        const auto beatSamples = samplesPerBeat_.load(std::memory_order_relaxed);
        const auto barBeats = beatsPerBar_.load(std::memory_order_relaxed);
        constexpr domain::ProjectSample pulseSamples = 96;
        for (std::size_t frame = 0; frame < block.frameCount; ++frame) {
            const auto position = start + static_cast<domain::ProjectSample>(frame);
            const auto phase = position % beatSamples;
            if (phase < 0 || phase >= pulseSamples) continue;
            const auto beat = position / beatSamples;
            const float accent = beat % barBeats == 0 ? 0.20F : 0.12F;
            const float click = accent
                * (1.0F - static_cast<float>(phase) / static_cast<float>(pulseSamples));
            for (std::size_t channel = 0; channel < block.outputChannels; ++channel) {
                block.outputs[channel][frame] = std::clamp(
                    block.outputs[channel][frame] + click, -1.0F, 1.0F);
            }
        }
    }

    playhead_.store(start + static_cast<domain::ProjectSample>(block.frameCount),
        std::memory_order_relaxed);
}

}  // namespace composer::audio
