#pragma once

#include <iostream>
#include <string_view>

namespace composer::tests {

class TestContext final {
public:
    explicit TestContext(std::string_view suite) : suite_(suite) {}

    void expect(const bool condition, const std::string_view message) {
        if (condition) {
            ++passes_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL [" << suite_ << "]: " << message << '\n';
    }

    [[nodiscard]] int finish() const {
        std::cout << suite_ << ": " << passes_ << " passed, " << failures_ << " failed\n";
        return failures_ == 0 ? 0 : 1;
    }

private:
    std::string_view suite_;
    int passes_{};
    int failures_{};
};

}  // namespace composer::tests
