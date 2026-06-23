#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace composer::application {

enum class ErrorCode {
    invalidArgument,
    staleRevision,
    deviceUnavailable,
    callbackDeadlineExceeded,
    dependencyUnavailable,
    recordingPathUnavailable,
    diskSpaceLow,
    audioWriteFailure,
    deviceDisconnected,
    sampleRateChanged,
    inputDropout,
    recordingBusy,
    recoveryFailure
};

[[nodiscard]] constexpr std::string_view errorId(const ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::invalidArgument: return "APP-ARGUMENT-001";
        case ErrorCode::staleRevision: return "APP-REVISION-002";
        case ErrorCode::deviceUnavailable: return "AUDIO-DEVICE-001";
        case ErrorCode::callbackDeadlineExceeded: return "AUDIO-DEADLINE-002";
        case ErrorCode::dependencyUnavailable: return "DEPENDENCY-001";
        case ErrorCode::recordingPathUnavailable: return "RECORD-PATH-001";
        case ErrorCode::diskSpaceLow: return "RECORD-DISK-002";
        case ErrorCode::audioWriteFailure: return "RECORD-WRITE-003";
        case ErrorCode::deviceDisconnected: return "RECORD-DEVICE-004";
        case ErrorCode::sampleRateChanged: return "RECORD-RATE-005";
        case ErrorCode::inputDropout: return "RECORD-DROPOUT-006";
        case ErrorCode::recordingBusy: return "RECORD-BUSY-007";
        case ErrorCode::recoveryFailure: return "RECORD-RECOVERY-008";
    }
    return "APP-UNKNOWN-999";
}

struct Error final {
    ErrorCode code{};
    std::string message;

    [[nodiscard]] constexpr std::string_view stableId() const noexcept { return errorId(code); }
};

template <typename Value>
using Result = std::variant<Value, Error>;

}  // namespace composer::application
