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
    dependencyUnavailable
};

[[nodiscard]] constexpr std::string_view errorId(const ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::invalidArgument: return "APP-ARGUMENT-001";
        case ErrorCode::staleRevision: return "APP-REVISION-002";
        case ErrorCode::deviceUnavailable: return "AUDIO-DEVICE-001";
        case ErrorCode::callbackDeadlineExceeded: return "AUDIO-DEADLINE-002";
        case ErrorCode::dependencyUnavailable: return "DEPENDENCY-001";
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
