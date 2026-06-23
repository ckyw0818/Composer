#pragma once

#include "audio/contracts/ProjectSnapshot.h"
#include "domain/Project.h"

#include <filesystem>

namespace composer::audio {

// Compiles an immutable, sample-domain ProjectSnapshot from a domain::Project using the project's
// TempoMap as the sole ticks->samples authority. Runs off the audio thread (model/worker side).
struct ProjectCompiler final {
    [[nodiscard]] static ProjectSnapshot compile(
        const domain::Project& project, domain::Revision revision,
        const std::filesystem::path& assetRoot = {});
};

}  // namespace composer::audio
