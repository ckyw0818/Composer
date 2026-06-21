#pragma once

#include "domain/Types.h"

#include <cmath>

namespace composer::domain {

// Single ticks <-> samples conversion authority for the whole project.
//
// S1 ships a constant-tempo map: one BPM and one sample rate for the entire timeline. The
// interface is shaped for a future piecewise-tempo map (S2+), but the rounding rules are frozen
// now so fixtures stay deterministic across slices: conversions round half-to-even on the exact
// rational sample count, never on accumulated floating-point error.
class TempoMap final {
public:
    constexpr TempoMap() noexcept = default;
    constexpr TempoMap(const double beatsPerMinute, const double sampleRate) noexcept
        : bpm_(beatsPerMinute), sampleRate_(sampleRate) {}

    [[nodiscard]] constexpr double beatsPerMinute() const noexcept { return bpm_; }
    [[nodiscard]] constexpr double sampleRate() const noexcept { return sampleRate_; }

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return bpm_ > 0.0 && sampleRate_ > 0.0;
    }

    // samples = ticks * (60 / bpm / PPQ) * sampleRate, evaluated as an exact integer ratio
    // numerator / denominator then rounded half-to-even. ticks and samplesPerMinute are integral,
    // so the only rounding happens once at the end.
    [[nodiscard]] ProjectSample ticksToSamples(const Tick ticks) const noexcept {
        // ticks * 60 * sampleRate / (bpm * PPQ). bpm and sampleRate may be fractional, so scale
        // them to integers via a fixed denominator before the divide.
        const long double numerator =
            static_cast<long double>(ticks) * 60.0L * static_cast<long double>(sampleRate_);
        const long double denominator =
            static_cast<long double>(bpm_) * static_cast<long double>(kPpq);
        return roundHalfToEven(numerator / denominator);
    }

    [[nodiscard]] Tick samplesToTicks(const ProjectSample samples) const noexcept {
        const long double numerator =
            static_cast<long double>(samples) * static_cast<long double>(bpm_)
            * static_cast<long double>(kPpq);
        const long double denominator = 60.0L * static_cast<long double>(sampleRate_);
        return roundHalfToEven(numerator / denominator);
    }

    [[nodiscard]] constexpr bool operator==(const TempoMap&) const noexcept = default;

private:
    [[nodiscard]] static std::int64_t roundHalfToEven(const long double value) noexcept {
        const long double floorValue = std::floor(value);
        const long double fraction = value - floorValue;
        auto result = static_cast<std::int64_t>(floorValue);
        if (fraction > 0.5L) {
            ++result;
        } else if (fraction == 0.5L) {
            if ((result & 1) != 0) {
                ++result;
            }
        }
        return result;
    }

    double bpm_{120.0};
    double sampleRate_{48000.0};
};

}  // namespace composer::domain
