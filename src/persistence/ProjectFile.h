#pragma once

#include "application/Result.h"
#include "domain/Project.h"

#include <filesystem>

namespace composer::persistence {

// Crash-conscious project.json adapter. Save writes and validates a temporary manifest, rotates the
// prior file to .bak, then renames the verified temporary file into place.
struct ProjectFile final {
    [[nodiscard]] static application::Result<std::monostate> save(
        const std::filesystem::path& path, const domain::Project& project);
    [[nodiscard]] static application::Result<domain::Project> load(
        const std::filesystem::path& path);
};

}  // namespace composer::persistence
