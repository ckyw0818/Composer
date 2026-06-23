#pragma once

#include <cstddef>

namespace composer::tests {

void resetRealtimeAllocationCount() noexcept;
[[nodiscard]] std::size_t realtimeAllocationCount() noexcept;

}  // namespace composer::tests
