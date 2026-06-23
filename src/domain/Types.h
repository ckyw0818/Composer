#pragma once

#include <cstdint>
#include <string>

namespace composer::domain {

using Tick = std::int64_t;
using ProjectSample = std::int64_t;
using Revision = std::uint64_t;
using Pitch = std::int32_t;
using Velocity = std::int32_t;

inline constexpr Tick kPpq = 960;

// Valid MIDI pitch and velocity ranges. Chord insertion and note edits clamp/validate
// against these so the domain never produces values an instrument cannot voice.
inline constexpr Pitch kMinPitch = 0;
inline constexpr Pitch kMaxPitch = 127;
inline constexpr Velocity kMinVelocity = 1;
inline constexpr Velocity kMaxVelocity = 127;

struct EntityId final {
    std::string value;

    [[nodiscard]] bool operator==(const EntityId&) const = default;
    [[nodiscard]] bool operator<(const EntityId& other) const { return value < other.value; }
    [[nodiscard]] bool empty() const noexcept { return value.empty(); }
};

struct HalfOpenTickRange final {
    Tick start{};
    Tick end{};

    [[nodiscard]] constexpr bool isValid() const noexcept { return start <= end; }
    [[nodiscard]] constexpr bool contains(const Tick tick) const noexcept {
        return tick >= start && tick < end;
    }
    [[nodiscard]] constexpr Tick length() const noexcept { return end - start; }

    [[nodiscard]] constexpr bool operator==(const HalfOpenTickRange&) const noexcept = default;
};

[[nodiscard]] constexpr bool isValidPitch(const Pitch pitch) noexcept {
    return pitch >= kMinPitch && pitch <= kMaxPitch;
}

[[nodiscard]] constexpr bool isValidVelocity(const Velocity velocity) noexcept {
    return velocity >= kMinVelocity && velocity <= kMaxVelocity;
}

}  // namespace composer::domain
