#pragma once

#include "audio/contracts/AudioRenderer.h"
#include "audio/contracts/RuntimeSnapshot.h"

namespace composer::audio {

class ClickRenderer final : public AudioRenderer {
public:
    explicit ClickRenderer(RuntimeSnapshot snapshot) noexcept;

    void prepare(const AudioSpec& spec) override;
    void render(const RenderBlock& block) noexcept override;

    [[nodiscard]] const AudioSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] const RuntimeSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    AudioSpec spec_{};
    RuntimeSnapshot snapshot_{};
};

}  // namespace composer::audio
