#pragma once

#include "application/Result.h"
#include "domain/Types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace composer::recording {

enum class JournalStatus {
    recording,
    recoverable,
    finalized,
    recovered
};

struct RecoveryJournalEntry final {
    std::string takeId;
    domain::EntityId trackId;
    std::filesystem::path partialPath;
    std::filesystem::path finalPath;
    domain::ProjectSample startSample{};
    double sampleRate{48000.0};
    int channels{1};
    JournalStatus status{JournalStatus::recording};
    std::int64_t framesWritten{};
    application::ErrorCode failure{application::ErrorCode::audioWriteFailure};

    [[nodiscard]] bool operator==(const RecoveryJournalEntry&) const = default;
};

struct RecoveryJournal final {
    [[nodiscard]] static application::Result<std::monostate> save(
        const std::filesystem::path& path, const RecoveryJournalEntry& entry);
    [[nodiscard]] static application::Result<RecoveryJournalEntry> load(
        const std::filesystem::path& path);
    [[nodiscard]] static std::vector<std::filesystem::path> pending(
        const std::filesystem::path& recoveryDirectory);
};

}  // namespace composer::recording
