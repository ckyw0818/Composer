#include "persistence/ProjectFile.h"

#include "persistence/ProjectSerializer.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <variant>

namespace composer::persistence {
namespace {

using application::Error;
using application::ErrorCode;

constexpr std::uintmax_t kMaxManifestBytes = 64ULL * 1024ULL * 1024ULL;

[[nodiscard]] Error fileError(const std::string& message) {
    return Error{ErrorCode::dependencyUnavailable, message};
}

}  // namespace

application::Result<domain::Project> ProjectFile::load(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return fileError("project file does not exist or is not readable");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxManifestBytes) {
        return fileError("project manifest is too large or its size cannot be read");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return fileError("project file could not be opened");
    }
    std::string json(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !input.read(json.data(), static_cast<std::streamsize>(size))) {
        return fileError("project file could not be read completely");
    }
    return ProjectSerializer::parse(json);
}

application::Result<std::monostate> ProjectFile::save(
    const std::filesystem::path& path, const domain::Project& project) {
    if (path.empty() || path.filename().empty()) {
        return fileError("project save path is empty");
    }
    const std::string json = ProjectSerializer::serialize(project);
    const auto parsed = ProjectSerializer::parse(json);
    if (std::holds_alternative<application::Error>(parsed)
        || std::get<domain::Project>(parsed) != project) {
        return fileError("project verification failed before save");
    }

    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    std::error_code error;
    std::filesystem::remove(temporary, error);
    error.clear();

    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) return fileError("temporary project file could not be opened");
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return fileError("temporary project file could not be written completely");
        }
    }

    const auto verified = load(temporary);
    if (std::holds_alternative<application::Error>(verified)
        || std::get<domain::Project>(verified) != project) {
        std::filesystem::remove(temporary, error);
        return fileError("temporary project file failed round-trip validation");
    }

    const bool hadOriginal = std::filesystem::is_regular_file(path, error) && !error;
    error.clear();
    if (hadOriginal) {
        std::filesystem::remove(backup, error);
        error.clear();
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return fileError("previous project could not be rotated to backup");
        }
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        if (hadOriginal) {
            std::error_code restoreError;
            std::filesystem::rename(backup, path, restoreError);
        }
        std::filesystem::remove(temporary, error);
        return fileError("verified project could not replace the active manifest");
    }
    return std::monostate{};
}

}  // namespace composer::persistence
