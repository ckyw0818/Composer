#pragma once

namespace composer::audio {

class RealtimeSafety final {
public:
    class Scope final {
    public:
        Scope() noexcept : previous_(active_) { active_ = true; }
        ~Scope() noexcept { active_ = previous_; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        bool previous_{};
    };

    [[nodiscard]] static bool isRealtimeThread() noexcept { return active_; }

private:
    static inline thread_local bool active_ = false;
};

}  // namespace composer::audio
