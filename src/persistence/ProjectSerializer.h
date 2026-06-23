#pragma once

#include "application/Result.h"
#include "domain/Project.h"

#include <string>

namespace composer::persistence {

// Serializes a domain::Project to and from the versioned project.json manifest text.
//
// The writer emits canonical, stable formatting (sorted keys are unnecessary because the schema
// is fixed-order; numbers use round-trip-safe formatting) so that
//   parse(serialize(p)) == p   and   serialize(parse(serialize(p))) == serialize(p)
// both hold. This is the determinism the S1 exit gate ("save/reload/render") depends on.
struct ProjectSerializer final {
    [[nodiscard]] static std::string serialize(const domain::Project& project);
    [[nodiscard]] static application::Result<domain::Project> parse(const std::string& json);
};

}  // namespace composer::persistence
