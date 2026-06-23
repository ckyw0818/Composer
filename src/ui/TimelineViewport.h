#pragma once

#include "domain/Types.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace composer::ui {

// Horizontal musical-time viewport shared by every piano-roll coordinate conversion.
// Tick zero, clip position, scroll, and zoom therefore cannot drift into separate formulas.
class TimelineViewport final {
public:
    static constexpr double kDefaultPixelsPerBeat = 48.0;
    static constexpr double kMinPixelsPerBeat = 16.0;
    static constexpr double kMaxPixelsPerBeat = 192.0;
    static constexpr domain::Tick kInitialHorizonTicks = domain::kPpq * 4 * 64;
    static constexpr domain::Tick kMaximumNavigationTick =
        std::numeric_limits<domain::Tick>::max() / 4;

    void setRange(const domain::HalfOpenTickRange range, const bool resetView) noexcept {
        const auto validRange = range.isValid() ? range : domain::HalfOpenTickRange{};
        if (resetView || validRange.start != range_.start) {
            range_.start = validRange.start;
            range_.end = std::max(validRange.end,
                saturatingAdd(validRange.start, kInitialHorizonTicks));
            viewStartTick_ = static_cast<double>(range_.start);
        } else {
            range_.end = std::max(range_.end, validRange.end);
        }
        clampViewStart();
    }

    void setViewportWidth(const double widthPixels) noexcept {
        widthPixels_ = std::max(0.0, widthPixels);
        clampViewStart();
    }

    [[nodiscard]] domain::Tick tickForX(const double x) const noexcept {
        return static_cast<domain::Tick>(std::floor(exactTickForX(x)));
    }

    [[nodiscard]] int xForTick(const domain::Tick tick) const noexcept {
        const double beats = (static_cast<double>(tick) - viewStartTick_)
            / static_cast<double>(domain::kPpq);
        return static_cast<int>(std::lround(beats * pixelsPerBeat_));
    }

    [[nodiscard]] domain::Tick snapNearest(
        const domain::Tick tick, const domain::Tick grid) const noexcept {
        if (grid <= 0) return clampToRange(tick);
        const auto nonNegative = std::max<domain::Tick>(0, tick);
        return clampToRange((nonNegative + grid / 2) / grid * grid);
    }

    [[nodiscard]] domain::Tick snapForInsertion(
        const domain::Tick tick, const domain::Tick grid, const bool bypass) const noexcept {
        if (bypass || grid <= 0) return clampEditableTick(tick);
        const auto nonNegative = std::max<domain::Tick>(0, tick);
        return clampEditableTick(nonNegative / grid * grid);
    }

    [[nodiscard]] domain::Tick clampToRange(const domain::Tick tick) const noexcept {
        return std::clamp(tick, range_.start, range_.end);
    }

    bool scrollByPixels(const double pixels) noexcept {
        const double before = viewStartTick_;
        const double proposedStart = viewStartTick_ + pixels * ticksPerPixel();
        if (pixels > 0.0) {
            const double visibleTicks = widthPixels_ * ticksPerPixel();
            ensureNavigationEnd(proposedStart + visibleTicks * 2.0);
        }
        viewStartTick_ = proposedStart;
        clampViewStart();
        return !approximatelyEqual(before, viewStartTick_);
    }

    bool zoomAt(const double anchorX, const double scaleFactor) noexcept {
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0) return false;
        const double oldPixelsPerBeat = pixelsPerBeat_;
        const double anchorTick = exactTickForX(anchorX);
        pixelsPerBeat_ = std::clamp(
            pixelsPerBeat_ * scaleFactor, kMinPixelsPerBeat, kMaxPixelsPerBeat);
        viewStartTick_ = anchorTick - anchorX * ticksPerPixel();
        ensureNavigationEnd(viewStartTick_ + widthPixels_ * ticksPerPixel());
        clampViewStart();
        return !approximatelyEqual(oldPixelsPerBeat, pixelsPerBeat_);
    }

    [[nodiscard]] double pixelsPerBeat() const noexcept { return pixelsPerBeat_; }
    [[nodiscard]] double viewStartTick() const noexcept { return viewStartTick_; }
    [[nodiscard]] domain::HalfOpenTickRange range() const noexcept { return range_; }

private:
    [[nodiscard]] double ticksPerPixel() const noexcept {
        return static_cast<double>(domain::kPpq) / pixelsPerBeat_;
    }

    [[nodiscard]] double exactTickForX(const double x) const noexcept {
        return viewStartTick_ + x * ticksPerPixel();
    }

    [[nodiscard]] domain::Tick clampEditableTick(const domain::Tick tick) const noexcept {
        if (range_.length() <= 0) return range_.start;
        return std::clamp(tick, range_.start, range_.end - 1);
    }

    void clampViewStart() noexcept {
        const double minimum = static_cast<double>(range_.start);
        const double visibleTicks = widthPixels_ * ticksPerPixel();
        const double maximum = std::max(
            minimum, static_cast<double>(range_.end) - visibleTicks);
        viewStartTick_ = std::clamp(viewStartTick_, minimum, maximum);
    }

    void ensureNavigationEnd(const double requiredEnd) noexcept {
        if (requiredEnd <= static_cast<double>(range_.end)) return;
        const double bounded = std::min(
            requiredEnd, static_cast<double>(kMaximumNavigationTick));
        const auto requiredTick = static_cast<domain::Tick>(std::ceil(bounded));
        range_.end = std::max(requiredTick,
            saturatingAdd(range_.end, kInitialHorizonTicks));
    }

    [[nodiscard]] static domain::Tick saturatingAdd(
        const domain::Tick lhs, const domain::Tick rhs) noexcept {
        if (lhs >= kMaximumNavigationTick - rhs) return kMaximumNavigationTick;
        return lhs + rhs;
    }

    [[nodiscard]] static bool approximatelyEqual(
        const double lhs, const double rhs) noexcept {
        return std::abs(lhs - rhs) < 0.0001;
    }

    domain::HalfOpenTickRange range_{};
    double widthPixels_{};
    double viewStartTick_{};
    double pixelsPerBeat_{kDefaultPixelsPerBeat};
};

}  // namespace composer::ui
