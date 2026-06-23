#include "recording/RecoveryJournal.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace composer::recording {
namespace {

using application::Error;
using application::ErrorCode;

std::string_view statusName(const JournalStatus status) {
    switch (status) {
        case JournalStatus::recording: return "recording";
        case JournalStatus::recoverable: return "recoverable";
        case JournalStatus::finalized: return "finalized";
        case JournalStatus::recovered: return "recovered";
    }
    return "recording";
}

bool parseStatus(const std::string& text, JournalStatus& status) {
    if (text == "recording") status = JournalStatus::recording;
    else if (text == "recoverable") status = JournalStatus::recoverable;
    else if (text == "finalized") status = JournalStatus::finalized;
    else if (text == "recovered") status = JournalStatus::recovered;
    else return false;
    return true;
}

template <typename Value>
bool readField(std::istream& input, const std::string_view expected, Value& value) {
    std::string name;
    return static_cast<bool>(input >> name >> value) && name == expected;
}

bool readQuotedField(
    std::istream& input, const std::string_view expected, std::string& value) {
    std::string name;
    return static_cast<bool>(input >> name >> std::quoted(value)) && name == expected;
}

}  // namespace

application::Result<std::monostate> RecoveryJournal::save(
    const std::filesystem::path& path, const RecoveryJournalEntry& entry) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return Error{ErrorCode::recordingPathUnavailable,
            "recording recovery directory could not be created"};
    }
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if (!output) {
        return Error{ErrorCode::recordingPathUnavailable,
            "recording recovery journal could not be created"};
    }
    output << "COMPOSER_RECORDING_JOURNAL 1\n"
           << "take " << std::quoted(entry.takeId) << '\n'
           << "track " << std::quoted(entry.trackId.value) << '\n'
           << "partial " << std::quoted(entry.partialPath.generic_string()) << '\n'
           << "final " << std::quoted(entry.finalPath.generic_string()) << '\n'
           << "start " << entry.startSample << '\n'
           << "sampleRate " << entry.sampleRate << '\n'
           << "channels " << entry.channels << '\n'
           << "status " << statusName(entry.status) << '\n'
           << "frames " << entry.framesWritten << '\n'
           << "failure " << static_cast<int>(entry.failure) << '\n';
    output.flush();
    if (!output) {
        output.close();
        std::filesystem::remove(temporary, error);
        return Error{ErrorCode::audioWriteFailure,
            "recording recovery journal could not be written"};
    }
    output.close();
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return Error{ErrorCode::audioWriteFailure,
            "recording recovery journal could not be committed"};
    }
    return std::monostate{};
}

application::Result<RecoveryJournalEntry> RecoveryJournal::load(
    const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    std::string magic;
    int version{};
    if (!(input >> magic >> version) || magic != "COMPOSER_RECORDING_JOURNAL" || version != 1) {
        return Error{ErrorCode::recoveryFailure, "recording journal header is invalid"};
    }
    RecoveryJournalEntry entry;
    std::string trackId;
    std::string partial;
    std::string final;
    std::string status;
    int failure{};
    if (!readQuotedField(input, "take", entry.takeId)
        || !readQuotedField(input, "track", trackId)
        || !readQuotedField(input, "partial", partial)
        || !readQuotedField(input, "final", final)
        || !readField(input, "start", entry.startSample)
        || !readField(input, "sampleRate", entry.sampleRate)
        || !readField(input, "channels", entry.channels)
        || !readField(input, "status", status)
        || !readField(input, "frames", entry.framesWritten)
        || !readField(input, "failure", failure)
        || !parseStatus(status, entry.status)) {
        return Error{ErrorCode::recoveryFailure, "recording journal fields are invalid"};
    }
    entry.trackId = domain::EntityId{trackId};
    entry.partialPath = std::filesystem::path{partial};
    entry.finalPath = std::filesystem::path{final};
    entry.failure = static_cast<ErrorCode>(failure);
    if (entry.takeId.empty() || entry.trackId.empty() || entry.partialPath.empty()
        || entry.finalPath.empty() || entry.sampleRate <= 0.0 || entry.channels <= 0) {
        return Error{ErrorCode::recoveryFailure, "recording journal metadata is invalid"};
    }
    return entry;
}

std::vector<std::filesystem::path> RecoveryJournal::pending(
    const std::filesystem::path& recoveryDirectory) {
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    if (!std::filesystem::is_directory(recoveryDirectory, error)) return paths;
    for (std::filesystem::directory_iterator it{recoveryDirectory, error}, end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == ".journal") {
            paths.push_back(it->path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

}  // namespace composer::recording
