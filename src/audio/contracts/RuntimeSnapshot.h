#pragma once

#include "domain/Types.h"

#include <array>
#include <cstdint>

namespace composer::audio {

struct ClickVoice final {
    std::int64_t intervalSamples{};
    std::int64_t phaseSamples{};
    std::int32_t pulseSamples{};
    float amplitude{};
};

struct RuntimeSnapshot final {
    domain::Revision revision{};
    std::int64_t lengthSamples{};
    std::array<ClickVoice, 4> voices{};
};

}  // namespace composer::audio
