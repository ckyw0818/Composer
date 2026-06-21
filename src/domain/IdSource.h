#pragma once

#include "domain/Types.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace composer::domain {

// Source of stable entity IDs. Abstracted so the canonical fixture and tests can produce
// deterministic, reproducible IDs while production uses a random UUID generator. IDs are opaque
// strings; the domain never parses them.
class IdSource {
public:
    virtual ~IdSource() = default;
    [[nodiscard]] virtual EntityId next() = 0;
};

// Deterministic counter-based IDs: "<prefix>-00000001", "<prefix>-00000002", ...
// Two runs with the same prefix and call order produce byte-identical projects.
class SequentialIdSource final : public IdSource {
public:
    explicit SequentialIdSource(std::string prefix) : prefix_(std::move(prefix)) {}

    [[nodiscard]] EntityId next() override {
        ++counter_;
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%08llu",
            static_cast<unsigned long long>(counter_));
        return EntityId{prefix_ + "-" + buffer};
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return counter_; }

private:
    std::string prefix_;
    std::uint64_t counter_{0};
};

}  // namespace composer::domain
