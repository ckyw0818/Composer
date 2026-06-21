#pragma once

#include "domain/Types.h"

#include <cstddef>

namespace composer::audio {

struct AudioSpec final {
    double sampleRate{48000.0};
    std::size_t maximumBlockSize{256};
    std::size_t outputChannels{2};
};

struct RenderBlock final {
    float* const* outputs{};
    std::size_t outputChannels{};
    std::size_t frameCount{};
    domain::ProjectSample startSample{};
};

class AudioRenderer {
public:
    virtual ~AudioRenderer() = default;
    virtual void prepare(const AudioSpec& spec) = 0;
    virtual void render(const RenderBlock& block) noexcept = 0;
};

}  // namespace composer::audio
