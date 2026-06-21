#include "audio/runtime/ClickRenderer.h"

#include <algorithm>
#include <cstdint>

namespace composer::audio {

ClickRenderer::ClickRenderer(RuntimeSnapshot snapshot) noexcept : snapshot_(snapshot) {}

void ClickRenderer::prepare(const AudioSpec& spec) {
    spec_ = spec;
}

void ClickRenderer::render(const RenderBlock& block) noexcept {
    for (std::size_t channel = 0; channel < block.outputChannels; ++channel) {
        std::fill_n(block.outputs[channel], block.frameCount, 0.0F);
    }

    for (std::size_t frame = 0; frame < block.frameCount; ++frame) {
        const auto sample = block.startSample + static_cast<domain::ProjectSample>(frame);
        if (sample < 0 || sample >= snapshot_.lengthSamples) {
            continue;
        }

        float mixed = 0.0F;
        for (const auto& voice : snapshot_.voices) {
            if (voice.intervalSamples <= 0 || sample < voice.phaseSamples) {
                continue;
            }

            const auto position = (sample - voice.phaseSamples) % voice.intervalSamples;
            if (position < voice.pulseSamples) {
                const auto decay = static_cast<float>(voice.pulseSamples - position)
                    / static_cast<float>(voice.pulseSamples);
                mixed += voice.amplitude * decay;
            }
        }

        const auto output = std::clamp(mixed, -1.0F, 1.0F);
        for (std::size_t channel = 0; channel < block.outputChannels; ++channel) {
            block.outputs[channel][frame] = output;
        }
    }
}

}  // namespace composer::audio
